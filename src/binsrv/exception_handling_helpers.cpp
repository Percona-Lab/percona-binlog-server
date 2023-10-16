#include "binsrv/exception_handling_helpers.hpp"

#include <cassert>
#include <exception>
#include <memory>
#include <source_location>
#include <string>
#include <system_error>

#include <boost/lexical_cast.hpp>

#include "binsrv/basic_logger.hpp"
#include "binsrv/log_severity.hpp"

namespace {

void handle_location_mixin(const binsrv::basic_logger_ptr &logger,
                           const std::exception &wrapped_exception) {
  assert(logger);
  using namespace std::string_literals;
  const auto *mixin = dynamic_cast<const std::source_location *>(
      std::addressof(wrapped_exception));
  if (mixin == nullptr) {
    return;
  }
  logger->log(binsrv::log_severity::debug, "in "s + mixin->function_name());
  // TODO: consider removing a few levels from the mixin->file_name() to
  // avoid printing "/home/<user>/..." prefixes
  logger->log(binsrv::log_severity::trace, "at "s + mixin->file_name() + ':' +
                                               std::to_string(mixin->line()) +
                                               ':' +
                                               std::to_string(mixin->column()));
}

} // anonymous namespace

namespace binsrv {

void handle_std_exception(const basic_logger_ptr &logger) {
  using namespace std::string_literals;
  if (!logger) {
    return;
  }
  try {
    throw;
  } catch (const std::system_error &e) {
    logger->log(log_severity::error, "std::system_error caught: "s + e.what());
    logger->log(log_severity::debug,
                boost::lexical_cast<std::string>(e.code()));
    handle_location_mixin(logger, e);
  } catch (const std::exception &e) {
    logger->log(log_severity::error, "std::exception caught: "s + e.what());
    handle_location_mixin(logger, e);
  } catch (...) {
    logger->log(log_severity::error, "unhandled exception caught");
  }
}

} // namespace binsrv
