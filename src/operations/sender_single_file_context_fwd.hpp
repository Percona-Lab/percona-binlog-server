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

#ifndef OPERATIONS_SENDER_SINGLE_FILE_CONTEXT_FWD_HPP
#define OPERATIONS_SENDER_SINGLE_FILE_CONTEXT_FWD_HPP

#include <cstdint>
#include <memory>

namespace operations {

class sender_single_file_context;

enum class event_fetching_result_type : std::uint8_t { success, waiting, eof };
using sender_single_file_context_ptr =
    std::unique_ptr<sender_single_file_context>;

} // namespace operations

#endif // OPERATIONS_SENDER_SINGLE_FILE_CONTEXT_FWD_HPP
