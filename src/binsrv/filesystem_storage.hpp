#ifndef BINSRV_FILESYSTEM_STORAGE_HPP
#define BINSRV_FILESYSTEM_STORAGE_HPP

#include "binsrv/filesystem_storage_fwd.hpp" // IWYU pragma: export

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "util/byte_span_fwd.hpp"

namespace binsrv {

class [[nodiscard]] filesystem_storage {
public:
  static constexpr std::string_view default_binlog_index_name{"binlog.index"};
  static constexpr std::string_view default_binlog_index_entry_path{"."};

  explicit filesystem_storage(std::string_view root_path);

  filesystem_storage(const filesystem_storage &) = delete;
  filesystem_storage &operator=(const filesystem_storage &) = delete;
  filesystem_storage(filesystem_storage &&) = delete;
  filesystem_storage &operator=(filesystem_storage &&) = delete;

  // desctuctor is explicitly defined as default here to complete the rule of 5
  ~filesystem_storage() = default;

  [[nodiscard]] const std::filesystem::path &get_root_path() const noexcept {
    return root_path_;
  }
  [[nodiscard]] std::string_view get_binlog_name() const noexcept {
    return binlog_names_.empty() ? std::string_view{} : binlog_names_.back();
  }
  [[nodiscard]] std::uint64_t get_position() const noexcept {
    return position_;
  }

  [[nodiscard]] static bool
  check_binlog_name(std::string_view binlog_name) noexcept;

  void open_binlog(std::string_view binlog_name);
  void write_event(util::const_byte_span event_data);
  void close_binlog();

private:
  std::filesystem::path root_path_;

  using binlog_name_container = std::vector<std::string>;
  binlog_name_container binlog_names_;
  std::ofstream ofs_;
  std::uint64_t position_{0ULL};

  [[nodiscard]] std::filesystem::path get_index_path() const;
  void load_binlog_index(const std::filesystem::path &index_path);
  void append_to_binlog_index(std::string_view binlog_name);
};

} // namespace binsrv

#endif // BINSRV_FILESYSTEM_STORAGE_HPP
