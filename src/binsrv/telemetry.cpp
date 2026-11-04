// Copyright (c) 2026 Percona and/or its affiliates.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License, version 2.0,
// as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License, version 2.0, for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

#include "binsrv/telemetry.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#pragma GCC diagnostic pop

#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>

#include "app_version.hpp"

#include "binsrv/basic_logger.hpp"
#include "binsrv/log_severity.hpp"
#include "binsrv/main_config.hpp"
#include "binsrv/telemetry_config.hpp"

#include "util/byte_span.hpp"
#include "util/file_operations_helpers.hpp"
#include "util/native_file_operations_helpers.hpp"
#include "util/nv_tuple.hpp"
#include "util/nv_tuple_to_json.hpp"
#include "util/uuid_helpers.hpp"

namespace binsrv {

namespace {

constexpr std::string_view tmp_extension{".tmp"};
constexpr std::string_view json_extension{".json"};
// Files from other instances are removed only once older than this upper bound,
// so a peer's recent reports are never deleted prematurely.
constexpr std::chrono::seconds unconditional_cleanup_interval{60 * 60 * 24 * 7};

// The report serialized to JSON: db_instance_id is this instance's persisted
// identifier, pillar_version is the PBS version and uptime is the number of
// whole seconds since reporting started.
struct telemetry_report
    : util::nv_tuple<util::nv<"db_instance_id", std::string>,
                     util::nv<"pillar_version", std::string>,
                     util::nv<"uptime", std::string>> {};

} // anonymous namespace

// Owns the timer and all report state. It is captured by the self-rescheduling
// timer callback, so it must stay put; 'telemetry' moves only the unique_ptr to
// it.
class telemetry::impl {
public:
  impl(boost::asio::io_context &io_ctx, basic_logger_ptr logger,
       const telemetry_config &telemetry_cfg, const main_config &config)
      : logger_{std::move(logger)}, path_{telemetry_cfg.path()},
        history_keep_interval_{telemetry_cfg.history_keep_interval()},
        db_instance_id_{config.instance_id()},
        pillar_version_{app_version.get_string()},
        tag_uuid_{util::generate_canonical_uuid()},
        start_time_{std::chrono::steady_clock::now()}, timer_{io_ctx},
        scrape_interval_{telemetry_cfg.scrape_interval()} {
    const auto grace_interval{telemetry_cfg.grace_interval()};
    // Report the effective intervals (after clamping into their permitted
    // ranges) once, when reporting starts.
    logger_->log_format(log_severity::info,
                        "telemetry: reporting to {} with grace_interval={}s, "
                        "scrape_interval={}s, history_keep_interval={}s",
                        path_, grace_interval.count(), scrape_interval_.count(),
                        history_keep_interval_.count());
    timer_.expires_after(grace_interval);
    arm();
  }
  impl(const impl &) = delete;
  impl(impl &&) = delete;
  impl &operator=(const impl &) = delete;
  impl &operator=(impl &&) = delete;
  ~impl() = default;

private:
  void arm() {
    // NOLINTNEXTLINE(misc-include-cleaner)
    timer_.async_wait([this](const boost::system::error_code &error_code) {
      if (error_code) {
        // cancelled, e.g. because the io_context is shutting down
        return;
      }
      write_report();
      timer_.expires_after(scrape_interval_);
      arm();
    });
  }

  void clean_old_reports(std::chrono::sys_seconds current_time) const noexcept {
    try {
      const auto oldest{current_time - history_keep_interval_};
      const auto oldest_unconditional{current_time -
                                      unconditional_cleanup_interval};

      for (const auto &entry : std::filesystem::directory_iterator{path_}) {
        const auto extension{entry.path().extension()};
        if (extension != tmp_extension && extension != json_extension) {
          continue;
        }
        // the stem is expected to be "<unix_ts>-<tag_uuid>"
        const auto stem{entry.path().stem().string()};
        const auto delimiter_pos{stem.find('-')};
        if (delimiter_pos == std::string::npos || delimiter_pos == 0U ||
            delimiter_pos == stem.length() - 1U) {
          continue;
        }
        const auto timestamp_str{stem.substr(0U, delimiter_pos)};
        const auto file_tag{stem.substr(delimiter_pos + 1U)};

        std::chrono::sys_seconds file_timestamp{};
        try {
          file_timestamp = std::chrono::sys_seconds{
              std::chrono::seconds{std::stoll(timestamp_str)}};
        } catch (const std::exception &) {
          continue;
        }

        const bool ours{file_tag == tag_uuid_};
        const bool expired{ours ? (file_timestamp < oldest)
                                : (file_timestamp < oldest_unconditional)};
        if (!expired) {
          continue;
        }

        std::error_code remove_ec;
        std::filesystem::remove(entry.path(), remove_ec);
        if (remove_ec) {
          logger_->log_format(log_severity::warning,
                              "telemetry: could not remove {}: {}",
                              entry.path().string(), remove_ec.message());
        }
      }
    } catch (const std::exception &e) {
      logger_->log_format(log_severity::warning,
                          "telemetry: report cleanup failed: {}", e.what());
    }
  }

  void write_report() noexcept {
    try {
      const auto current_time{
          std::chrono::time_point_cast<std::chrono::seconds>(
              std::chrono::system_clock::now())};

      clean_old_reports(current_time);

      const auto uptime{std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::steady_clock::now() - start_time_)};

      telemetry_report report{};
      report.get<"db_instance_id">() = db_instance_id_;
      report.get<"pillar_version">() = pillar_version_;
      report.get<"uptime">() = std::to_string(uptime.count());
      boost::json::value json_value;
      util::nv_tuple_to_json(json_value, report);
      const std::string body{boost::json::serialize(json_value)};

      const std::filesystem::path directory{path_};
      const std::string base_name{
          std::to_string(current_time.time_since_epoch().count()) + "-" +
          tag_uuid_};
      const auto tmp_path{directory / (base_name + std::string{tmp_extension})};
      const auto json_path{directory /
                           (base_name + std::string{json_extension})};

      // atomic write: temp -> fsync -> rename -> fsync directory
      util::write_file_content("telemetry report temporary file", tmp_path,
                               util::as_const_byte_span(body));
      util::fsync(tmp_path);

      std::error_code rename_ec;
      std::filesystem::rename(tmp_path, json_path, rename_ec);
      if (rename_ec) {
        logger_->log_format(log_severity::warning,
                            "telemetry: could not finalize {}: {}",
                            json_path.string(), rename_ec.message());
        return;
      }

      std::error_code permissions_ec;
      std::filesystem::permissions(json_path,
                                   std::filesystem::perms::owner_read |
                                       std::filesystem::perms::owner_write |
                                       std::filesystem::perms::group_read |
                                       std::filesystem::perms::others_read,
                                   std::filesystem::perm_options::replace,
                                   permissions_ec);

      util::fsync(directory);
      logger_->log_format(log_severity::info, "telemetry: wrote report {}",
                          json_path.string());
    } catch (const std::exception &e) {
      logger_->log_format(log_severity::warning,
                          "telemetry: report write failed: {}", e.what());
    } catch (...) {
      logger_->log(log_severity::warning,
                   "telemetry: report write failed with an unknown error");
    }
  }

  basic_logger_ptr logger_;
  std::string path_;
  std::chrono::seconds history_keep_interval_;
  std::string db_instance_id_;
  std::string pillar_version_;
  // Random per-process tag embedded in our report filenames, so cleanup can
  // tell our own files from those of other instances sharing the directory.
  std::string tag_uuid_;
  // Captured when reporting starts, so that uptime can be measured from here.
  std::chrono::steady_clock::time_point start_time_;
  boost::asio::steady_timer timer_;
  std::chrono::seconds scrape_interval_;
};

telemetry telemetry::start(boost::asio::io_context &io_ctx,
                           basic_logger_ptr logger, const main_config &config) {
  const telemetry_config telemetry_cfg{
      config.root().get<"telemetry">().value_or(telemetry_config{})};
  if (!telemetry_cfg.enabled()) {
    return telemetry{nullptr};
  }
  return telemetry{
      std::make_unique<impl>(io_ctx, std::move(logger), telemetry_cfg, config)};
}

telemetry::telemetry(std::unique_ptr<impl> pimpl) noexcept
    : impl_{std::move(pimpl)} {}

telemetry::telemetry(telemetry &&) noexcept = default;
telemetry &telemetry::operator=(telemetry &&) noexcept = default;
telemetry::~telemetry() = default;

} // namespace binsrv
