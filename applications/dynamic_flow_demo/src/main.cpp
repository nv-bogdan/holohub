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
  bool edit_mode = false;
  bool apply_changes = false;
  std::mutex mutex;
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
    op_output.emit(value, "out");
  }

 private:
  int counter_ = 0;
};

// Generic pass-through operator
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

// Router Operator - dynamically routes based on configuration
class RouterOperator : public holoscan::Operator {
 public:
  HOLOSCAN_OPERATOR_FORWARD_ARGS(RouterOperator)

  RouterOperator() = default;

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
    viz::Init(1024, 768, "Holoscan Dynamic Workflow Editor");
  }

  void compute(holoscan::InputContext& op_input, holoscan::OutputContext&,
               holoscan::ExecutionContext&) override {
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
      return;
    }

    viz::Begin();
    viz::BeginImGuiLayer();
    
    std::lock_guard<std::mutex> lock(g_config->mutex);
    
    ImGui::Begin("Dynamic Workflow Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("Holoscan Dynamic Flow Demo");
    ImGui::Separator();
    ImGui::Spacing();
    
    // Edit/Apply button
    if (g_config->edit_mode) {
      if (ImGui::Button("Apply", ImVec2(100, 30))) {
        g_config->apply_changes = true;
        // Keep all operators in order, just update enabled/disabled state
        g_config->operator_order = g_config->pending_order;
        g_config->operator_enabled = g_config->pending_enabled;
        g_config->edit_mode = false;
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
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "EDIT MODE - Drag to reorder, check/uncheck to enable/disable");
    } else {
      if (ImGui::Button("Edit", ImVec2(100, 30))) {
        g_config->edit_mode = true;
        g_config->pending_order = g_config->operator_order;
        g_config->pending_enabled = g_config->operator_enabled;
        std::cout << "Entering edit mode..." << std::endl;
      }
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Current workflow display
    ImGui::Text("Workflow:");
    ImGui::Spacing();
    
    // Source (fixed)
    ImGui::Text("Source:");
    ImGui::SameLine(180);
    if (source_val_ >= 0) {
      ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Value: %d", source_val_);
    } else {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Waiting...");
    }
    ImGui::Text("    |");
    ImGui::Text("    v");
    
    // Display operators based on mode
    auto& display_order = g_config->edit_mode ? g_config->pending_order : g_config->operator_order;
    
    if (g_config->edit_mode) {
      // Edit mode: draggable operators with enable/disable checkboxes
      for (int i = 0; i < display_order.size(); i++) {
        ImGui::PushID(i);
        
        std::string op_name = display_order[i];
        bool enabled = g_config->pending_enabled[op_name];
        
        // Checkbox for enable/disable
        if (ImGui::Checkbox("", &enabled)) {
          g_config->pending_enabled[op_name] = enabled;
          std::cout << "Operator " << op_name << " " << (enabled ? "enabled" : "disabled") << std::endl;
        }
        ImGui::SameLine();
        
        // Draggable button (styled differently if disabled)
        if (!enabled) {
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        }
        
        std::string button_label = op_name + (enabled ? "" : " (disabled)");
        ImGui::Button(button_label.c_str(), ImVec2(200, 40));
        
        if (!enabled) {
          ImGui::PopStyleColor(3);
        }
        
        // Drag source
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
              std::cout << "Swapped: " << display_order[source_idx] << " <-> " << display_order[i] << std::endl;
            }
          }
          ImGui::EndDragDropTarget();
        }
        
        ImGui::PopID();
        
        if (enabled || i < display_order.size() - 1) {
          ImGui::Text("    |");
          ImGui::Text("    v");
        }
      }
    } else {
      // Run mode: show enabled operators with values
      for (const auto& op_name : display_order) {
        // Only show if enabled
        if (!g_config->operator_enabled[op_name]) continue;
        
        std::string display_name = "Operator " + op_name;
        ImGui::Text("%s:", display_name.c_str());
        ImGui::SameLine(180);
        
        int value = -1;
        if (op_name == "one") value = one_val_;
        else if (op_name == "two") value = two_val_;
        else if (op_name == "three") value = three_val_;
        
        if (value >= 0) {
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Value: %d", value);
        } else {
          ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Waiting...");
        }
        
        ImGui::Text("    |");
        ImGui::Text("    v");
      }
    }
    
    // Sink (fixed)
    ImGui::Text("Sink:");
    ImGui::SameLine(180);
    if (sink_val_ >= 0) {
      ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Value: %d", sink_val_);
    } else {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Waiting...");
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Show current order (only enabled operators)
    std::string order_str = "Current order: source -> ";
    bool first = true;
    for (const auto& op : g_config->operator_order) {
      if (g_config->operator_enabled[op]) {
        if (!first) order_str += " -> ";
        order_str += op;
        first = false;
      }
    }
    if (first) {
      order_str += "(no operators)";
    }
    order_str += " -> sink";
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", order_str.c_str());
    
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
    
    // Create ImGui visualizer
    auto visualizer = make_operator<ImGuiVisualizer>("visualizer",
                                                       make_condition<holoscan::PeriodicCondition>("periodic", 
                                                                                                   holoscan::Arg("recess_period", std::string("30Hz"))));

    // For now: simple static connections (we'll add dynamic flow later)
    // Connect in default order: source -> one -> two -> three -> sink
    add_flow(source, op_one, {{"out", "in"}});
    add_flow(op_one, op_two, {{"out", "in"}});
    add_flow(op_two, op_three, {{"out", "in"}});
    add_flow(op_three, sink, {{"out", "in"}});
    
    // Connect to visualizer for monitoring
    add_flow(source, visualizer, {{"out", "source_in"}});
    add_flow(op_one, visualizer, {{"out", "one_in"}});
    add_flow(op_two, visualizer, {{"out", "two_in"}});
    add_flow(op_three, visualizer, {{"out", "three_in"}});
    add_flow(op_three, visualizer, {{"out", "sink_in"}});
    
    add_operator(visualizer);
  }
};

int main(int argc, char** argv) {
  auto app = holoscan::make_application<App>();
  app->run();
  return 0;
}
