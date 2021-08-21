#include "RDGCore.h"

#include "RDGPartHeader.h"
#include "katana/Result.h"
#include "tsuba/Errors.h"
#include "tsuba/ParquetReader.h"

namespace {

katana::Result<void>
UpsertProperties(
    const std::shared_ptr<arrow::Table>& props,
    std::shared_ptr<arrow::Table>* to_update,
    std::vector<tsuba::PropStorageInfo>* prop_state) {
  if (!props->schema()->HasDistinctFieldNames()) {
    return KATANA_ERROR(
        tsuba::ErrorCode::Exists, "column names must be distinct: {}",
        fmt::join(props->schema()->field_names(), ", "));
  }

  if (prop_state->empty()) {
    KATANA_LOG_ASSERT((*to_update)->num_columns() == 0);
    for (const auto& field : props->fields()) {
      prop_state->emplace_back(field->name(), field->type());
    }
    *to_update = props;
    return katana::ResultSuccess();
  }

  std::shared_ptr<arrow::Table> next = *to_update;

  if (next->num_columns() > 0 && next->num_rows() != props->num_rows()) {
    return KATANA_ERROR(
        tsuba::ErrorCode::InvalidArgument, "expected {} rows found {} instead",
        next->num_rows(), props->num_rows());
  }

  int last = next->num_columns();

  for (int i = 0, n = props->num_columns(); i < n; i++) {
    std::shared_ptr<arrow::Field> field = props->field(i);
    int current_col = -1;

    auto prop_info_it = std::find_if(
        prop_state->begin(), prop_state->end(),
        [&](const tsuba::PropStorageInfo& psi) {
          return field->name() == psi.name();
        });
    if (prop_info_it == prop_state->end()) {
      prop_info_it = prop_state->insert(
          prop_info_it, tsuba::PropStorageInfo(field->name(), field->type()));
    } else if (!prop_info_it->IsAbsent()) {
      current_col = next->schema()->GetFieldIndex(field->name());
    }

    if (current_col < 0) {
      if (next->num_columns() == 0) {
        next = arrow::Table::Make(arrow::schema({field}), {props->column(i)});
      } else {
        next = KATANA_CHECKED_CONTEXT(
            next->AddColumn(last++, field, props->column(i)), "insert");
      }
    } else {
      next = KATANA_CHECKED_CONTEXT(
          next->SetColumn(current_col, field, props->column(i)), "update");
    }
    prop_info_it->WasModified(field->type());
  }

  if (!next->schema()->HasDistinctFieldNames()) {
    return KATANA_ERROR(
        tsuba::ErrorCode::Exists, "column names are not distinct: {}",
        fmt::join(next->schema()->field_names(), ", "));
  }

  *to_update = next;

  return katana::ResultSuccess();
}

katana::Result<void>
AddProperties(
    const std::shared_ptr<arrow::Table>& props,
    std::shared_ptr<arrow::Table>* to_update,
    std::vector<tsuba::PropStorageInfo>* prop_state) {
  for (const auto& field : props->fields()) {
    // Column names are not sorted, but assumed to be less than 100s
    auto prop_info_it = std::find_if(
        prop_state->begin(), prop_state->end(),
        [&](const tsuba::PropStorageInfo& psi) {
          return field->name() == psi.name();
        });

    if (prop_info_it != prop_state->end()) {
      return KATANA_ERROR(
          tsuba::ErrorCode::Exists, "column names are not distinct");
    }
  }
  return UpsertProperties(props, to_update, prop_state);
}

katana::Result<void>
EnsureTypeLoaded(const katana::Uri& rdg_dir, tsuba::PropStorageInfo* psi) {
  if (!psi->type()) {
    auto reader = KATANA_CHECKED(tsuba::ParquetReader::Make());
    KATANA_LOG_ASSERT(psi->IsAbsent());
    std::shared_ptr<arrow::Schema> schema =
        KATANA_CHECKED(reader->GetSchema(rdg_dir.Join(psi->path())));
    psi->set_type(schema->field(0)->type());
  }
  return katana::ResultSuccess();
}

}  // namespace

namespace tsuba {

katana::Result<void>
RDGCore::AddNodeProperties(const std::shared_ptr<arrow::Table>& props) {
  return AddProperties(
      props, &node_properties_, &part_header_.node_prop_info_list());
}

katana::Result<void>
RDGCore::AddEdgeProperties(const std::shared_ptr<arrow::Table>& props) {
  return AddProperties(
      props, &edge_properties_, &part_header_.edge_prop_info_list());
}

katana::Result<void>
RDGCore::UpsertNodeProperties(const std::shared_ptr<arrow::Table>& props) {
  return UpsertProperties(
      props, &node_properties_, &part_header_.node_prop_info_list());
}

katana::Result<void>
RDGCore::UpsertEdgeProperties(const std::shared_ptr<arrow::Table>& props) {
  return UpsertProperties(
      props, &edge_properties_, &part_header_.edge_prop_info_list());
}

katana::Result<void>
RDGCore::EnsureNodeTypesLoaded(const katana::Uri& rdg_dir) {
  for (auto& prop : part_header_.node_prop_info_list()) {
    KATANA_CHECKED_CONTEXT(
        EnsureTypeLoaded(rdg_dir, &prop), "property {}",
        std::quoted(prop.name()));
  }
  return katana::ResultSuccess();
}

katana::Result<void>
RDGCore::EnsureEdgeTypesLoaded(const katana::Uri& rdg_dir) {
  for (auto& prop : part_header_.edge_prop_info_list()) {
    KATANA_CHECKED_CONTEXT(
        EnsureTypeLoaded(rdg_dir, &prop), "property {}",
        std::quoted(prop.name()));
  }
  return katana::ResultSuccess();
}

void
RDGCore::InitEmptyProperties() {
  std::vector<std::shared_ptr<arrow::Array>> empty;
  node_properties_ = arrow::Table::Make(arrow::schema({}), empty, 0);
  edge_properties_ = arrow::Table::Make(arrow::schema({}), empty, 0);
}

bool
RDGCore::Equals(const RDGCore& other) const {
  // Assumption: t_f_s and other.t_f_s are both fully loaded into memory
  return topology_file_storage_.size() == other.topology_file_storage_.size() &&
         !memcmp(
             topology_file_storage_.ptr<uint8_t>(),
             other.topology_file_storage_.ptr<uint8_t>(),
             topology_file_storage_.size()) &&
         node_properties_->Equals(*other.node_properties_, true) &&
         edge_properties_->Equals(*other.edge_properties_, true);
}

katana::Result<void>
RDGCore::RemoveNodeProperty(int i) {
  auto field = node_properties_->field(i);
  node_properties_ = KATANA_CHECKED(node_properties_->RemoveColumn(i));

  return part_header_.RemoveNodeProperty(field->name());
}

katana::Result<void>
RDGCore::RemoveEdgeProperty(int i) {
  auto field = edge_properties_->field(i);
  edge_properties_ = KATANA_CHECKED(edge_properties_->RemoveColumn(i));

  return part_header_.RemoveEdgeProperty(field->name());
}

}  // namespace tsuba
