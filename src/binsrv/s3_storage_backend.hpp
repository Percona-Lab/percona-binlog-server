#ifndef BINSRV_S3_STORAGE_BACKEND_HPP
#define BINSRV_S3_STORAGE_BACKEND_HPP

#include <filesystem>
#include <string_view>

#include "binsrv/basic_storage_backend.hpp" // IWYU pragma: export

namespace binsrv {

class [[nodiscard]] s3_storage_backend : public basic_storage_backend {
public:
  explicit s3_storage_backend(std::string_view root_path);

  [[nodiscard]] const std::filesystem::path &get_root_path() const noexcept {
    return root_path_;
  }

private:
  std::filesystem::path root_path_;
  struct options_deleter {
    void operator()(void *ptr) const noexcept;
  };
  using options_ptr = std::unique_ptr<void, options_deleter>;
  options_ptr options_;

  [[nodiscard]] storage_object_name_container do_list_objects() override;

  [[nodiscard]] std::string do_get_object(std::string_view name) override;
  void do_put_object(std::string_view name,
                     util::const_byte_span content) override;

  void do_open_stream(std::string_view name) override;
  void do_write_data_to_stream(util::const_byte_span data) override;
  void do_close_stream() override;

  [[nodiscard]] std::string do_get_description() const override;
};

} // namespace binsrv

#endif // BINSRV_S3_STORAGE_BACKEND_HPP
