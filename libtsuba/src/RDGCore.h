#ifndef KATANA_LIBTSUBA_RDGCORE_H_
#define KATANA_LIBTSUBA_RDGCORE_H_

#include <memory>

#include <arrow/api.h>

#include "RDGPartHeader.h"
#include "katana/config.h"
#include "tsuba/FileView.h"

namespace tsuba {

class KATANA_EXPORT RDGCore {
public:
  RDGCore() { InitEmptyProperties(); }

  RDGCore(RDGPartHeader&& part_header) : part_header_(std::move(part_header)) {
    InitEmptyProperties();
  }

  bool Equals(const RDGCore& other) const;

  katana::Result<void> AddNodeProperties(
      const std::shared_ptr<arrow::Schema>& add_schema);

  katana::Result<void> RemoveNodeProperties(
      const std::shared_ptr<arrow::Schema>& remove_schema);

  katana::Result<void> SetNodePropertyData(
      const std::string& name, std::shared_ptr<arrow::Array> prop_arr);

  katana::Result<void> AddEdgeProperties(
      const std::shared_ptr<arrow::Schema>& add_schema);

  katana::Result<void> RemoveEdgeProperties(
      const std::shared_ptr<arrow::Schema>& remove_schema);

  katana::Result<void> SetEdgePropertyData(
      const std::string& name, std::shared_ptr<arrow::Array> prop_arr);

  //
  // Accessors and Mutators
  //

  void drop_node_properties() {
    std::vector<std::shared_ptr<arrow::Array>> empty;
    node_properties_ = arrow::Table::Make(arrow::schema({}), empty, 0);
  }
  void drop_edge_properties() {
    std::vector<std::shared_ptr<arrow::Array>> empty;
    edge_properties_ = arrow::Table::Make(arrow::schema({}), empty, 0);
  }

  const FileView& topology_file_storage() const {
    return topology_file_storage_;
  }
  FileView& topology_file_storage() { return topology_file_storage_; }
  void set_topology_file_storage(FileView&& topology_file_storage) {
    topology_file_storage_ = std::move(topology_file_storage);
  }

  const RDGPartHeader& part_header() const { return part_header_; }
  RDGPartHeader& part_header() { return part_header_; }
  void set_part_header(RDGPartHeader&& part_header) {
    part_header_ = std::move(part_header);
  }

  katana::Result<void> RegisterTopologyFile(const std::string& new_top) {
    part_header_.set_topology_path(new_top);
    return topology_file_storage_.Unbind();
  }

private:
  void InitEmptyProperties();

  //
  // Data
  //

  /// The node and edge schemas are loaded from disk and stay in memory
  std::shared_ptr<arrow::Table> node_schema_;
  std::shared_ptr<arrow::Table> edge_schema_;

  /// Property columns are cached in memory.  This vector has the same number of
  /// entries as the schema.  If the data is absent, there is a nullptr at that index.
  std::vector<std::shared_ptr<arrow::Array>> node_prop_data_;
  std::vector<std::shared_ptr<arrow::Array>> edge_prop_data_;

  FileView topology_file_storage_;

  RDGPartHeader part_header_;
};

}  // namespace tsuba

#endif
