#ifndef EASYMYSQL_BINLOG_HPP
#define EASYMYSQL_BINLOG_HPP

#include "easymysql/binlog_fwd.hpp"

#include <cstdint>
#include <memory>
#include <span>

#include "easymysql/connection_fwd.hpp"

namespace easymysql {

class [[nodiscard]] binlog {
  friend class connection;

public:
  binlog() = default;

  binlog(const binlog &) = delete;
  binlog(binlog &&) = default;
  binlog &operator=(const binlog &) = delete;
  binlog &operator=(binlog &&) = default;

  ~binlog();

  [[nodiscard]] bool is_empty() const noexcept { return !impl_; }

  // returns empty span on EOF
  // throws an exception on error
  binlog_stream_span fetch();

private:
  binlog(connection &conn, std::uint32_t server_id);

  connection *conn_{nullptr};
  struct rpl_deleter {
    void operator()(void *ptr) const noexcept;
  };
  using impl_ptr = std::unique_ptr<void, rpl_deleter>;
  impl_ptr impl_;
};

} // namespace easymysql

#endif // EASYMYSQL_BINLOG_HPP
