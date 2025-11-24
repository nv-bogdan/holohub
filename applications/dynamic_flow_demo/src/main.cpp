/*
 * Place the license header here
 */

#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <algorithm>
#include <map>
#include <imgui.h>
#include <holoscan/holoscan.hpp>
#include <holoviz/holoviz.hpp>

// Shared state for workflow configuration
struct WorkflowConfig {
  std::vector<std::string> operator_order = {"one", "two", "three"};
  std::vector<std::string> pending_order = {"one", "two", "three"};
  std::map<std::string, bool> operator_enabled = {{"one", true}, {"two", true}, {"three", true}};
  std::map<std::string, bool> pending_enabled = {{"one", true}, {"two", true}, {"three", true}};
  bool has_pending_changes = false;
  int current_stage = 0;  // Track which stage we're at (0 = start, 1-3 = after each op)
  std::mutex mutex;
  
  // Get list of enabled operators in order
  std::vector<std::string> get_enabled_operators() const {
    std::vector<std::string> enabled;
    for (const auto& op : operator_order) {
      if (operator_enabled.at(op)) {
        enabled.push_back(op);
      }
    }
    return enabled;
  }
};

static std::shared_ptr<WorkflowConfig> g_config = std::make_shared<WorkflowConfig>();

// Source Operator - generates incremental values
class SourceOperator : public holoscan::Operator {
 public:
  HOLOSCAN_OPERATOR_FORWARD_ARGS(SourceOperator)

  SourceOperator() = default;

  void setup(holoscan::OperatorSpec& spec) override {
    spec.output<int>("out");
  }

  void compute(holoscan::InputContext&, holoscan::OutputContext& op_output,
               holoscan::ExecutionContext&) override {
    int value = counter_++;
    std::cout << "Source: " << value << std::endl;
    // Reset stage counter for new data
    {
      std::lock_guard<std::mutex> lock(g_config->mutex);
      g_config->current_stage = 0;
    }
    op_output.emit(value, "out");
  }

 private:
  int counter_ = 0;
};

// Generic pass-through operator - tracks stage
class PassThroughOperator : public holoscan::Operator {
 public:
  HOLOSCAN_OPERATOR_FORWARD_ARGS(PassThroughOperator)

  PassThroughOperator() = default;

  void setup(holoscan::OperatorSpec& spec) override {
    spec.input<int>("in");
    spec.output<int>("out");
  }

  void compute(holoscan::InputContext& op_input, holoscan::OutputContext& op_output,
               holoscan::ExecutionContext&) override {
    auto value = op_input.receive<int>("in").value();
    std::cout << this->name() << ": " << value << std::endl;
    // Increment stage after processing
    {
      std::lock_guard<std::mutex> lock(g_config->mutex);
      g_config->current_stage++;
    }
    op_output.emit(value, "out");
  }
};

// Sink Operator
class SinkOperator : public holoscan::Operator {
 public:
  HOLOSCAN_OPERATOR_FORWARD_ARGS(SinkOperator)

  SinkOperator() = default;

  void setup(holoscan::OperatorSpec& spec) override {
    spec.input<int>("in");
  }

  void compute(holoscan::InputContext& op_input, holoscan::OutputContext&,
               holoscan::ExecutionContext&) override {
    auto value = op_input.receive<int>("in").value();
    std::cout << "Sink: " << value << std::endl;
  }
};

// Main Router Operator - routes based on current stage
class MainRouterOperator : public holoscan::Operator {
 public:
  HOLOSCAN_OPERATOR_FORWARD_ARGS(MainRouterOperator)

  MainRouterOperator() = default;

  void setup(holoscan::OperatorSpec& spec) override {
    spec.input<int>("in");
    spec.output<int>("out");
  }

  void compute(holoscan::InputContext& op_input, holoscan::OutputContext& op_output,
               holoscan::ExecutionContext&) override {
    auto value = op_input.receive<int>("in");
    if (value) {
      op_output.emit(value.value(), "out");
    }
  }
};

// ImGui Visualizer with workflow editing
class ImGuiVisualizer : public holoscan::Operator {
 public:
  HOLOSCAN_OPERATOR_FORWARD_ARGS(ImGuiVisualizer)

  ImGuiVisualizer() = default;

  void setup(holoscan::OperatorSpec& spec) override {
    spec.input<int>("source_in").condition(holoscan::ConditionType::kNone);
    spec.input<int>("one_in").condition(holoscan::ConditionType::kNone);
    spec.input<int>("two_in").condition(holoscan::ConditionType::kNone);
    spec.input<int>("three_in").condition(holoscan::ConditionType::kNone);
    spec.input<int>("sink_in").condition(holoscan::ConditionType::kNone);
  }

  void start() override {
    namespace viz = holoscan::viz;
    viz::Init(500, 650, "Holoscan Dynamic Workflow Editor");
  }

  void compute(holoscan::InputContext& op_input, holoscan::OutputContext&,
               holoscan::ExecutionContext& context) override {
    namespace viz = holoscan::viz;
    
    // Receive values from all operators
    auto source_value = op_input.receive<int>("source_in");
    auto one_value = op_input.receive<int>("one_in");
    auto two_value = op_input.receive<int>("two_in");
    auto three_value = op_input.receive<int>("three_in");
    auto sink_value = op_input.receive<int>("sink_in");
    
    // Update stored values if received
    if (source_value) source_val_ = source_value.value();
    if (one_value) one_val_ = one_value.value();
    if (two_value) two_val_ = two_value.value();
    if (three_value) three_val_ = three_value.value();
    if (sink_value) sink_val_ = sink_value.value();
    
    if (viz::WindowShouldClose()) {
      std::cout << "Window closed, stopping application..." << std::endl;
      // Shutdown visualization and exit cleanly
      viz::Shutdown();
      std::exit(0);
      return;
    }

    viz::Begin();
    viz::BeginImGuiLayer();
    
    std::lock_guard<std::mutex> lock(g_config->mutex);
    
    ImGui::Begin("Dynamic Workflow Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("Holoscan Dynamic Flow Demo");
    ImGui::Separator();
    ImGui::Spacing();
    
    // Apply button (enabled only when there are pending changes)
    ImGui::BeginDisabled(!g_config->has_pending_changes);
    if (ImGui::Button("Apply", ImVec2(100, 30))) {
      // Apply changes
      g_config->operator_order = g_config->pending_order;
      g_config->operator_enabled = g_config->pending_enabled;
      g_config->has_pending_changes = false;
      std::cout << "Applying workflow changes..." << std::endl;
      
      // Print new order (only enabled ones)
      std::cout << "New active order: ";
      bool first = true;
      for (const auto& op : g_config->operator_order) {
        if (g_config->operator_enabled[op]) {
          if (!first) std::cout << " -> ";
          std::cout << op;
          first = false;
        }
      }
      if (first) std::cout << "(no operators)";
      std::cout << std::endl;
    }
    ImGui::EndDisabled();
    
    if (g_config->has_pending_changes) {
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "*");
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Unsaved changes");
    }
    
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Drag to reorder, check/uncheck to enable/disable");
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Current workflow display
    ImGui::Text("Workflow:");
    ImGui::Spacing();
    
    // Source (fixed)
    ImGui::Text("Source:");
    ImGui::SameLine(220);
    if (source_val_ >= 0) {
      ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Value: %d", source_val_);
    } else {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Waiting...");
    }
    ImGui::Text("    |");
    ImGui::Text("    v");
    
    // Always show draggable operators with enable/disable checkboxes
    auto& display_order = g_config->pending_order;
    
    for (int i = 0; i < display_order.size(); i++) {
      ImGui::PushID(i);
      
      std::string op_name = display_order[i];
      bool enabled = g_config->pending_enabled[op_name];
      
      // Checkbox for enable/disable
      if (ImGui::Checkbox("", &enabled)) {
        g_config->pending_enabled[op_name] = enabled;
        g_config->has_pending_changes = true;
        std::cout << "Operator " << op_name << " " << (enabled ? "enabled" : "disabled") << std::endl;
      }
      ImGui::SameLine();
      
      // Get current value for this operator
      int op_value = -1;
      if (op_name == "one") op_value = one_val_;
      else if (op_name == "two") op_value = two_val_;
      else if (op_name == "three") op_value = three_val_;
      
      // Draggable area using Selectable instead of Button
      std::string item_label = op_name;
      if (!enabled) item_label += " (disabled)";
      
      // Style the selectable differently if disabled
      if (!enabled) {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
      } else {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.26f, 0.59f, 0.98f, 0.4f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.26f, 0.59f, 0.98f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.26f, 0.59f, 0.98f, 0.8f));
      }
      
      ImGui::Selectable(item_label.c_str(), false, 0, ImVec2(120, 40));
      
      ImGui::PopStyleColor(3);
      
      // Drag source - attach to the selectable
      if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload("OPERATOR", &i, sizeof(int));
        ImGui::Text("Moving %s", display_order[i].c_str());
        ImGui::EndDragDropSource();
      }
      
      // Drop target
      if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OPERATOR")) {
          int source_idx = *(const int*)payload->Data;
          if (source_idx != i) {
            std::swap(display_order[source_idx], display_order[i]);
            g_config->has_pending_changes = true;
            std::cout << "Swapped: " << display_order[source_idx] << " <-> " << display_order[i] << std::endl;
          }
        }
        ImGui::EndDragDropTarget();
      }
      
      // Show value next to item
      ImGui::SameLine();
      if (op_value >= 0 && enabled) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Value: %d", op_value);
      } else if (enabled) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Waiting...");
      }
      
      ImGui::PopID();
      ImGui::Text("    |");
      ImGui::Text("    v");
    }
    
    // Sink (fixed)
    ImGui::Text("Sink:");
    ImGui::SameLine(220);
    if (sink_val_ >= 0) {
      ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Value: %d", sink_val_);
    } else {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Waiting...");
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Show active order (currently running)
    std::string active_order_str = "Active workflow: source -> ";
    bool first = true;
    for (const auto& op : g_config->operator_order) {
      if (g_config->operator_enabled[op]) {
        if (!first) active_order_str += " -> ";
        active_order_str += op;
        first = false;
      }
    }
    if (first) {
      active_order_str += "(no operators)";
    }
    active_order_str += " -> sink";
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", active_order_str.c_str());
    
    ImGui::Spacing();
    ImGui::Text("Press ESC or close window to exit");
    
    ImGui::End();
    
    viz::EndLayer();
    viz::End();
  }

  void stop() override {
    namespace viz = holoscan::viz;
    viz::Shutdown();
  }

 private:
  int source_val_ = -1;
  int one_val_ = -1;
  int two_val_ = -1;
  int three_val_ = -1;
  int sink_val_ = -1;
};

class App : public holoscan::Application {
 public:
  void compose() override {
    // Create source operator - run periodically at 1Hz
    auto source = make_operator<SourceOperator>("source", 
                                                 make_condition<holoscan::PeriodicCondition>("periodic",
                                                                                             holoscan::Arg("recess_period", std::string("1Hz"))));
    
    // Create three pass-through operators
    auto op_one = make_operator<PassThroughOperator>("one");
    auto op_two = make_operator<PassThroughOperator>("two");
    auto op_three = make_operator<PassThroughOperator>("three");
    
    // Create sink operator
    auto sink = make_operator<SinkOperator>("sink");
    
    // Create main router for dynamic routing
    auto router = make_operator<MainRouterOperator>("router");
    
    // Create ImGui visualizer
    auto visualizer = make_operator<ImGuiVisualizer>("visualizer",
                                                       make_condition<holoscan::PeriodicCondition>("periodic", 
                                                                                                   holoscan::Arg("recess_period", std::string("30Hz"))));

    // Connect source to router
    add_flow(source, router, {{"out", "in"}});
    
    // Connect all operators back to router (for next stage)
    add_flow(op_one, router, {{"out", "in"}});
    add_flow(op_two, router, {{"out", "in"}});
    add_flow(op_three, router, {{"out", "in"}});
    
    // Set up dynamic routing from router
    set_dynamic_flows(router, [op_one, op_two, op_three, sink](const std::shared_ptr<holoscan::Operator>& op) {
      std::lock_guard<std::mutex> lock(g_config->mutex);
      
      auto enabled_ops = g_config->get_enabled_operators();
      int stage = g_config->current_stage;
      
      std::cout << "[Router] Stage " << stage << ", enabled ops: " << enabled_ops.size() << std::endl;
      
      // Route based on current stage
      if (stage < enabled_ops.size()) {
        const auto& target_op = enabled_ops[stage];
        std::cout << "[Router] Routing to operator: " << target_op << std::endl;
        if (target_op == "one") op->add_dynamic_flow(op_one);
        else if (target_op == "two") op->add_dynamic_flow(op_two);
        else if (target_op == "three") op->add_dynamic_flow(op_three);
      } else {
        std::cout << "[Router] Routing to sink" << std::endl;
        op->add_dynamic_flow(sink);
      }
    });
    
    // Connect to visualizer for monitoring
    add_flow(source, visualizer, {{"out", "source_in"}});
    add_flow(op_one, visualizer, {{"out", "one_in"}});
    add_flow(op_two, visualizer, {{"out", "two_in"}});
    add_flow(op_three, visualizer, {{"out", "three_in"}});
    add_flow(router, visualizer, {{"out", "sink_in"}});
    
    add_operator(visualizer);
  }
};

int main(int argc, char** argv) {
  auto app = holoscan::make_application<App>();
  app->run();
  return 0;
}
