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

#ifndef BINSRV_EVENTS_EVENT_VIEW_HPP
#define BINSRV_EVENTS_EVENT_VIEW_HPP

#include "binsrv/events/event_view_fwd.hpp" // IWYU pragma: export

#include "binsrv/events/common_header_view.hpp" // IWYU pragma: export
#include "binsrv/events/footer_view.hpp"        // IWYU pragma: export
#include "binsrv/events/protocol_traits_fwd.hpp"
#include "binsrv/events/reader_context_fwd.hpp"

#include "util/byte_span_fwd.hpp"

namespace binsrv::events {

class [[nodiscard]] event_view_base {
public:
  [[nodiscard]] util::const_byte_span get_portion() const noexcept {
    return portion_;
  }
  [[nodiscard]] util::byte_span get_updatable_portion() const noexcept {
    return portion_;
  }

  [[nodiscard]] std::size_t get_total_size() const noexcept {
    return std::size(get_portion());
  }

  [[nodiscard]] std::uint32_t calculate_crc() const noexcept;

  // common header section
  [[nodiscard]] static std::size_t get_common_header_size() noexcept {
    return default_common_header_length;
  }
  [[nodiscard]] util::const_byte_span get_common_header_raw() const noexcept {
    return get_portion().subspan(0U, get_common_header_size());
  }
  [[nodiscard]] util::byte_span
  get_common_header_updatable_raw() const noexcept {
    return get_updatable_portion().subspan(0U, get_common_header_size());
  }
  [[nodiscard]] common_header_view get_common_header_view() const;
  [[nodiscard]] common_header_updatable_view
  get_common_header_updatable_view() const;

  // post header section
  [[nodiscard]] std::size_t get_post_header_size() const noexcept {
    return post_header_size_;
  }
  [[nodiscard]] util::const_byte_span get_post_header_raw() const noexcept {
    return get_portion().subspan(get_common_header_size(),
                                 get_post_header_size());
  }
  [[nodiscard]] util::byte_span get_post_header_updatable_raw() const noexcept {
    return get_updatable_portion().subspan(get_common_header_size(),
                                           get_post_header_size());
  }

  // body section
  [[nodiscard]] std::size_t get_body_size() const noexcept {
    return get_total_size() - get_common_header_size() -
           get_post_header_size() - get_footer_size();
  }
  [[nodiscard]] util::const_byte_span get_body_raw() const noexcept {
    return get_portion().subspan(
        get_common_header_size() + get_post_header_size(), get_body_size());
  }
  [[nodiscard]] util::byte_span get_body_updatable_raw() const noexcept {
    return get_updatable_portion().subspan(
        get_common_header_size() + get_post_header_size(), get_body_size());
  }

  // footer section
  [[nodiscard]] std::size_t get_footer_size() const noexcept {
    return footer_size_;
  }
  [[nodiscard]] bool has_footer() const noexcept {
    return get_footer_size() != 0U;
  }
  [[nodiscard]] util::const_byte_span get_footer_raw() const noexcept {
    return get_portion().subspan(get_total_size() - get_footer_size(),
                                 get_footer_size());
  }
  [[nodiscard]] util::byte_span get_footer_updatable_raw() const noexcept {
    return get_updatable_portion().subspan(get_total_size() - get_footer_size(),
                                           get_footer_size());
  }
  [[nodiscard]] footer_view get_footer_view() const;
  [[nodiscard]] footer_updatable_view get_footer_updatable_view() const;

protected:
  event_view_base(const reader_context &context, util::byte_span portion);
  event_view_base(const event_view_base &other) = default;
  event_view_base(event_view_base &&other) = default;
  event_view_base &operator=(const event_view_base &other) = default;
  event_view_base &operator=(event_view_base &&other) = default;
  ~event_view_base() = default;

private:
  util::byte_span portion_{};
  std::size_t post_header_size_{0U};
  std::size_t footer_size_{0U};
};

class [[nodiscard]] event_updatable_view : private event_view_base {
  friend class event_view;

public:
  // an auxiliary proxy class that allows to update the content of an
  // event_view and automatically recalculates the checksum in the footer
  // (if present)

  // the designed usage scenario for this class is the following:
  //
  // const binsrv::events::reader_context context{...};
  // const util::const_byte_span ro_event_data_block{...};
  // const binsrv::events::event_view event_v{context, ro_event_data_block};
  // using event_buffer_type = std::vector<std::byte>;
  // event_buffer_type event_copy(std::cbegin(ro_event_data_block),
  //                              std::cend(ro_event_data_block));
  // const binsrv::events::event_updatable_view event_copy_uv{context,
  //                                                          event_copy};
  // {
  //   const auto proxy{event_copy_uv.get_write_proxy()};
  //   proxy.get_common_header_updatable_view()
  //        .set_next_event_position_raw(42U);
  // }
  //
  // please notice a new code block with the write_proxy object: it is
  // crucial to keep it alive when you want to perform several modyfying
  // operations - the checksum will be recalculated only at the end of
  // this code block when the proxy is destroyed
  class [[nodiscard]] write_proxy {
    friend class event_updatable_view;

  public:
    write_proxy(const write_proxy &other) = delete;
    write_proxy(write_proxy &&other) = delete;
    write_proxy &operator=(const write_proxy &other) = delete;
    write_proxy &operator=(write_proxy &&other) = delete;
    ~write_proxy() {
      if (parent_->has_footer()) {
        const auto footer_v{parent_->get_footer_updatable_view()};
        footer_v.set_crc_raw(parent_->calculate_crc());
      }
    }

    [[nodiscard]] common_header_updatable_view
    get_common_header_updatable_view() const {
      return parent_->get_common_header_updatable_view();
    }

    [[nodiscard]] util::byte_span
    get_post_header_updatable_raw() const noexcept {
      return parent_->get_post_header_updatable_raw();
    }

    [[nodiscard]] util::byte_span get_body_updatable_raw() const noexcept {
      return parent_->get_body_updatable_raw();
    }

  private:
    explicit write_proxy(const event_updatable_view &parent)
        : parent_{&parent} {}

    const event_view_base *parent_;
  };

  event_updatable_view(const reader_context &context, util::byte_span portion)
      : event_view_base{context, portion} {}

  // clang-format off
  using event_view_base::get_total_size;
  using event_view_base::calculate_crc;

  // common header section
  using event_view_base::get_common_header_size;

  // post header section
  using event_view_base::get_post_header_size;

  // body section
  using event_view_base::get_body_size;

  // footer section
  using event_view_base::get_footer_size;
  using event_view_base::has_footer;
  // clang-format on

  [[nodiscard]] write_proxy get_write_proxy() const {
    return write_proxy{*this};
  }
};

class [[nodiscard]] event_view : private event_view_base {
public:
  event_view(const reader_context &context, util::const_byte_span portion)
      : event_view_base{
            context,
            util::byte_span{
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
                const_cast<std::byte *>(std::data(portion)),
                std::size(portion)}} {}

  // deliberately implicit to allow seamless convertion from
  // event_updatable_view to event_view
  // NOLINTNEXTLINE(hicpp-explicit-conversions)
  event_view(const event_updatable_view &other) : event_view_base{other} {}

  // clang-format off
  using event_view_base::get_total_size;
  using event_view_base::calculate_crc;

  // common header section
  using event_view_base::get_common_header_size;
  using event_view_base::get_common_header_raw;
  using event_view_base::get_common_header_view;

  // post header section
  using event_view_base::get_post_header_size;
  using event_view_base::get_post_header_raw;

  // body section
  using event_view_base::get_body_size;
  using event_view_base::get_body_raw;

  // footer section
  using event_view_base::get_footer_size;
  using event_view_base::has_footer;
  using event_view_base::get_footer_raw;
  using event_view_base::get_footer_view;
  // clang-format on
};

} // namespace binsrv::events

#endif // BINSRV_EVENTS_EVENT_VIEW_HPP
