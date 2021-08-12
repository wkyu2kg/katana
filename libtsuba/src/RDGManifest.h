#ifndef KATANA_LIBTSUBA_RDGMANIFEST_H_
#define KATANA_LIBTSUBA_RDGMANIFEST_H_

#include <cstdint>
#include <regex>
#include <set>

#include "katana/JSON.h"
#include "katana/Logging.h"
#include "katana/RDGVersion.h"
#include "katana/Random.h"
#include "katana/URI.h"
#include "katana/config.h"
#include "tsuba/RDGLineage.h"
#include "tsuba/tsuba.h"

namespace tsuba {

static const char* kDefaultRDGViewType = "rdg";

// Struct version of main graph metadatafile
class KATANA_EXPORT RDGManifest {
  static const std::regex kManifestVersion;

  katana::Uri dir_;  // not persisted; inferred from name

  //
  // Persisted
  //

  katana::RDGVersion version_{katana::RDGVersion(0)};
  katana::RDGVersion previous_version_{katana::RDGVersion(0)};

  uint32_t num_hosts_{0};  // 0 is a reserved value for the empty RDG when
  // tsuba views policy_id as zero (not partitioned) or not zero (partitioned
  // according to a CuSP-specific policy)
  uint32_t policy_id_{0};
  bool transpose_{false};
  RDGLineage lineage_;
  std::string view_type_;
  std::vector<std::string> view_args_;

  RDGManifest(katana::Uri dir)
      : dir_(std::move(dir)), view_type_(kDefaultRDGViewType) {}
  RDGManifest(katana::Uri dir, const std::string& view_type)
      : dir_(std::move(dir)) {
    view_type_ = kDefaultRDGViewType;
    if (!view_type.empty()) {
      view_type_ = view_type;
    }
  }

  RDGManifest(
      katana::RDGVersion version, katana::RDGVersion previous_version,
      uint32_t num_hosts, uint32_t policy_id, bool transpose, katana::Uri dir,
      RDGLineage lineage)
      : dir_(std::move(dir)),
        version_(version),
        previous_version_(previous_version),
        num_hosts_(num_hosts),
        policy_id_(policy_id),
        transpose_(transpose),
        lineage_(std::move(lineage)) {}

  static katana::Result<RDGManifest> MakeFromStorage(const katana::Uri& uri);

  static std::string PartitionFileName(
      const std::string& view_type, uint32_t node_id,
      katana::RDGVersion version);

  std::string view_specifier() const {
    if (view_args_.size())
      return fmt::format("{}-{}", view_type_, fmt::join(view_args_, "-"));
    return view_type_;
  }

public:
  RDGManifest() = default;

  RDGManifest NextVersion(
      uint32_t num_hosts, uint32_t policy_id, bool transpose,
      const RDGLineage& lineage) const {
    katana::RDGVersion next_version(version_.numbers_, version_.branches_);
    next_version.IncrementNumber();
    // progress the version numbers
    return RDGManifest(
        next_version, version_, num_hosts, policy_id, transpose, dir_, lineage);
  }

  RDGManifest SameVersion(
      uint32_t num_hosts, uint32_t policy_id, bool transpose,
      const RDGLineage& lineage) const {
    // retain both current and previous versions
    return RDGManifest(
        version_, previous_version_, num_hosts, policy_id, transpose, dir_,
        lineage);
  }

  bool IsEmptyRDG() const { return num_hosts() == 0; }

  static katana::Result<RDGManifest> Make(RDGHandle handle);

  /// Create an RDGManifest
  /// \param uri a uri that either names a registered RDG or an explicit
  ///    rdg file
  /// \returns the constructed RDGManifest and the directory of its contents
  static katana::Result<RDGManifest> Make(const katana::Uri& uri);

  /// Create an RDGManifest
  /// \param uri is a storage prefix where the RDG is stored
  /// \param view_type is the view_type of the desired RDG
  /// \param version is the version of the RDG to load
  /// \returns the constructed RDGManifest and the directory of its contents
  static katana::Result<RDGManifest> Make(
      const katana::Uri& uri, const std::string& view_type,
      katana::RDGVersion version);

  const katana::Uri& dir() const { return dir_; }
  katana::RDGVersion version() const { return version_; }
  uint32_t num_hosts() const { return num_hosts_; }
  uint32_t policy_id() const { return policy_id_; }
  katana::RDGVersion previous_version() const { return previous_version_; }
  const std::string& viewtype() const { return view_type_; }
  void set_viewtype(std::string v) { view_type_ = v; }
  void set_viewargs(std::vector<std::string> v) { view_args_ = v; }
  void set_version(katana::RDGVersion val) { version_ = val; }
  void set_previous_version(katana::RDGVersion val) { previous_version_ = val; }
  void increment_version() { version_.IncrementNumber(); }
  const std::string& view_type() const { return view_type_; }
  bool transpose() const { return transpose_; }

  void set_dir(katana::Uri dir) { dir_ = std::move(dir); }

  katana::Uri PartitionFileName(uint32_t host_id) const;

  katana::Uri FileName() { return FileName(dir_, view_type_, version_); }

  // Canonical naming
  static katana::Uri FileName(
      const katana::Uri& uri, const std::string& view_type,
      katana::RDGVersion version);

  static katana::Uri PartitionFileName(
      const katana::Uri& uri, uint32_t node_id, katana::RDGVersion version);

  static katana::Uri PartitionFileName(
      const std::string& view_type, const katana::Uri& uri, uint32_t node_id,
      katana::RDGVersion version);

  static katana::Result<katana::RDGVersion> ParseVersionFromName(
      const std::string& file);
  static katana::Result<std::string> ParseViewNameFromName(
      const std::string& file);
  static katana::Result<std::vector<std::string>> ParseViewArgsFromName(
      const std::string& file);

  static bool IsManifestUri(const katana::Uri& uri);

  std::string ToJsonString() const;

  /// Return the set of file names that hold this RDG's data by reading partition files
  /// Useful to garbage collect unused files
  katana::Result<std::set<std::string>> FileNames();

  // Required by nlohmann
  friend void to_json(nlohmann::json& j, const RDGManifest& manifest);
  friend void from_json(const nlohmann::json& j, RDGManifest& manifest);
};

KATANA_EXPORT void to_json(nlohmann::json& j, const RDGManifest& manifest);
KATANA_EXPORT void from_json(const nlohmann::json& j, RDGManifest& manifest);

}  // namespace tsuba

#endif
