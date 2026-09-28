// Copyright (c) 2026 Percona and/or its affiliates.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License, version 2.0,
// as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

#ifndef BINSRV_INDEXED_EVENT_BLOCK_FWD_HPP
#define BINSRV_INDEXED_EVENT_BLOCK_FWD_HPP

#include <memory>
namespace binsrv {

class indexed_event_block;
using indexed_event_block_ptr = std::unique_ptr<indexed_event_block>;

} // namespace binsrv

#endif // BINSRV_INDEXED_EVENT_BLOCK_FWD_HPP
