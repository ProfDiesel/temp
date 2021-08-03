#pragma once

#include "config/config_reader.hpp"
#include "model/wiring.hpp"

#include <boilerplate/logger.hpp>

#include <boost/leaf/error.hpp>

#include <cstdlib>
#include <system_error>
#include <tuple>

inline auto make_handlers(auto &&printer) noexcept
{
  using namespace logger::literals;

  return std::tuple {[printer](const config::parse_error &error, const boost::leaf::e_source_location &location) noexcept
                     {
                       printer(logger::critical, "location=\"{}:{} {}\" first={} last={} expected={} actual={} Parse error"_format, location.file,
                               location.line, location.function, error.indices.first, error.indices.second, error.which, error.snippet);
                       std::abort();
                     },
                     [printer](const invalid_trigger_config &invalid_trigger_config, const boost::leaf::e_source_location &location) noexcept
                     {
                       printer(logger::critical, "location=\"{}:{} {}\" config={} Invalid trigger config"_format, location.file, location.line,
                               location.function, static_cast<const void *>(invalid_trigger_config.walker.object.get()));
                       std::abort();
                     },
                     [printer](const missing_field &missing_field, const boost::leaf::e_source_location &location) noexcept
                     {
                       printer(logger::critical, "location=\"{}:{} {}\" field={} Missing field"_format, location.file, location.line, location.function,
                               missing_field.field.data());
                       std::abort();
                     },
                     [printer](const std::error_code &error_code, const boost::leaf::e_source_location &location, std::string_view statement) noexcept
                     {
                       printer(logger::critical, "location=\"{}:{} {}\" statement={}, code={} {}"_format, location.file, location.line, location.function, 
                               statement, error_code.value(), error_code.message());
                       std::abort();
                     },
                     [printer](const std::error_code &error_code, const boost::leaf::e_source_location &location) noexcept
                     {
                       printer(logger::critical, "location=\"{}:{} {}\" code={} {}"_format, location.file, location.line, location.function, error_code.value(),
                               error_code.message());
                       std::abort();
                     },
                     [printer](const boost::leaf::e_source_location &location) noexcept
                     {
                       printer(logger::critical, "location=\"{}:{} {}\""_format, location.file, location.line, location.function);
                       std::abort();
                     },
                     [printer](const std::error_code &error_code) noexcept
                     {
                       printer(logger::critical, "code={} {}"_format, error_code.value(), error_code.message());
                       std::abort();
                     },
                     [printer](const boost::leaf::error_info &unmatched) noexcept
                     {
                       printer(logger::critical, fmt::to_string(unmatched));
                       std::abort();
                     }};
}
