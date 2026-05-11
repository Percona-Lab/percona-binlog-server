// Copyright (c) 2023-2024 Percona and/or its affiliates.
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

#ifndef BINSRV_BINLOG_FILE_METADATA_HPP
#define BINSRV_BINLOG_FILE_METADATA_HPP

#include "binsrv/binlog_file_metadata_fwd.hpp" // IWYU pragma: export

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "binsrv/ctime_timestamp.hpp"

#include "binsrv/gtids/gtid_set.hpp"

#include "util/nv_tuple.hpp"

namespace binsrv {

class [[nodiscard]] binlog_file_metadata {
private:
  // The two `*_renumberer_*` fields persist the rewrite-mode GTID
  // renumberer's recovery state for this binlog file. They are stored
  // here (rather than re-derived from the binlog file content on
  // resume) because every other piece of resume state already lives in
  // this metadata record - keeping the renumberer state alongside it
  // makes recovery a single read-and-load step. Both are
  // std::optional<> so that legacy metadata files written by older
  // binlog-server builds (which lacked these fields) load cleanly with
  // both values defaulting to std::nullopt; on next save the up-to-date
  // values are written back.
  using impl_type = util::nv_tuple<
      // clang-format off
      util::nv<"version", std::uint32_t>,
      util::nv<"size", std::uint64_t>,
      util::nv<"previous_gtids", gtids::optional_gtid_set>,
      util::nv<"added_gtids", gtids::optional_gtid_set>,
      util::nv<"min_timestamp", ctime_timestamp>,
      util::nv<"max_timestamp", ctime_timestamp>,
      util::nv<"renumberer_next_local_seq", std::optional<std::int64_t>>,
      util::nv<"renumberer_last_emitted_offset",
               std::optional<std::int64_t>>
      // clang-format on
      >;

public:
  binlog_file_metadata();

  explicit binlog_file_metadata(std::string_view data);

  [[nodiscard]] std::string str() const;

  [[nodiscard]] auto &root() noexcept { return impl_; }
  [[nodiscard]] const auto &root() const noexcept { return impl_; }

private:
  impl_type impl_;

  void validate() const;
};

} // namespace binsrv

#endif // BINSRV_BINLOG_FILE_METADATA_HPP
