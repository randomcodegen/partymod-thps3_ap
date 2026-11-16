#pragma once

#include <cstring>
#include <json/json.h>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>

namespace thps3_ap {

#include "gap_catalog.inc"

inline Json::Value ExpandGapSelection(const Json::Value &selection) {
  if (!selection.isObject() || !selection["ids"].isArray())
    throw std::runtime_error("Invalid THPS3 gap selection.");
  Json::Value gaps(Json::arrayValue);
  if (selection["ids"].empty())
    return gaps;
  if (!selection["catalog"].isString() ||
      selection["catalog"].asString() != kGapCatalogFingerprint)
    throw std::runtime_error("THPS3 gap catalogue mismatch. Use the client "
                             "that matches the seed's APWorld.");

  static const auto catalog = [] {
    const Json::CharReaderBuilder builder;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::map<int, Json::Value> rows;
    for (const auto *text : kGapCatalogRows) {
      Json::Value row;
      std::string error;
      if (!reader->parse(text, text + std::strlen(text), &row, &error))
        throw std::runtime_error("Invalid bundled THPS3 gap catalogue.");
      const int id = row["location_id"].asInt();
      rows.emplace(id, std::move(row));
    }
    return rows;
  }();
  std::set<int> seen;
  for (const auto &id : selection["ids"]) {
    if (id.type() != Json::intValue && id.type() != Json::uintValue)
      throw std::runtime_error("Invalid location ID in THPS3 gap selection.");
    const auto row = id.isInt() ? catalog.find(id.asInt()) : catalog.end();
    if (row == catalog.end())
      throw std::runtime_error("Unknown location ID in THPS3 gap selection.");
    if (!seen.insert(row->first).second)
      throw std::runtime_error("Duplicate location ID in THPS3 gap selection.");
    gaps.append(row->second);
  }
  return gaps;
}

} // namespace thps3_ap
