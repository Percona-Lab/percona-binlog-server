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

#ifndef OPENSSLPP_DIGEST_CONTEXT_FWD_HPP
#define OPENSSLPP_DIGEST_CONTEXT_FWD_HPP

#include <cstdint>

namespace opensslpp {

enum class digest_code_type : std::uint8_t { sha256, delimiter };

class digest_context;

} // namespace opensslpp

#endif // OPENSSLPP_DIGEST_CONTEXT_FWD_HPP
