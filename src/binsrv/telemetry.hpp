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

#ifndef BINSRV_TELEMETRY_HPP
#define BINSRV_TELEMETRY_HPP

#include "binsrv/telemetry_fwd.hpp" // IWYU pragma: export

#include <memory>

#include "binsrv/basic_logger_fwd.hpp"
#include "binsrv/main_config_fwd.hpp"

namespace boost::asio {
class io_context;
} // namespace boost::asio

namespace binsrv {

// Periodically writes JSON telemetry reports into the configured directory. The
// timer and file handling are hidden behind an opaque implementation; a live
// telemetry object owns a running schedule and stops it when destroyed. Report
// writing never throws: failures are caught and logged as warnings so telemetry
// cannot disrupt the hosting operation.
class [[nodiscard]] telemetry {
public:
  // Starts periodic reporting on 'io_ctx' when the configuration enables it
  // (telemetry is on by default, even when the section is absent). The report
  // contents are taken from 'config' and the running binary. The first report
  // is written after the grace interval and one every scrape interval
  // thereafter; the schedule stops when 'io_ctx' stops or this object is
  // destroyed. Keep the returned object alive while reporting should continue;
  // a disabled telemetry is a valid but inert object.
  [[nodiscard]] static telemetry start(boost::asio::io_context &io_ctx,
                                       basic_logger_ptr logger,
                                       const main_config &config);

  telemetry(telemetry &&) noexcept;
  telemetry &operator=(telemetry &&) noexcept;
  telemetry(const telemetry &) = delete;
  telemetry &operator=(const telemetry &) = delete;
  ~telemetry();

private:
  class impl;

  explicit telemetry(std::unique_ptr<impl> pimpl) noexcept;

  std::unique_ptr<impl> impl_;
};

} // namespace binsrv

#endif // BINSRV_TELEMETRY_HPP
