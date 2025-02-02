// Copyright 2021, PickNik Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//----------------------------------------------------------------------
/*!\file
 *
 * \author  Lovro Ivanov lovro.ivanov@gmail.com
 * \date    2021-11-18
 *
 */
//----------------------------------------------------------------------

#include "picknik_twist_controller/picknik_twist_controller.hpp"

#include <memory>
#include <string>
#include <vector>

#include "controller_interface/helpers.hpp"
#include "hardware_interface/loaned_command_interface.hpp"

namespace picknik_twist_controller
{
using hardware_interface::LoanedCommandInterface;

PicknikTwistController::PicknikTwistController()
: controller_interface::ControllerInterface(),
  rt_command_ptr_(nullptr),
  twist_command_subscriber_(nullptr)
{
}

controller_interface::InterfaceConfiguration
PicknikTwistController::command_interface_configuration() const
{
  controller_interface::InterfaceConfiguration command_interfaces_config;
  command_interfaces_config.type = controller_interface::interface_configuration_type::INDIVIDUAL;

  for (const auto & interface : interface_names_)
  {
    command_interfaces_config.names.push_back(joint_name_ + "/" + interface);
  }

  return command_interfaces_config;
}

controller_interface::InterfaceConfiguration PicknikTwistController::state_interface_configuration()
  const
{
  return controller_interface::InterfaceConfiguration{
    controller_interface::interface_configuration_type::NONE};
}

CallbackReturn PicknikTwistController::on_init()
{
  try
  {
    auto_declare<std::vector<std::string>>("interface_names", std::vector<std::string>());

    auto_declare<std::string>("joint", "");
  }
  catch (const std::exception & e)
  {
    fprintf(stderr, "Exception thrown during init stage with message: %s \n", e.what());
    return CallbackReturn::ERROR;
  }

  return CallbackReturn::SUCCESS;
}

CallbackReturn PicknikTwistController::on_configure(
  const rclcpp_lifecycle::State & /*previous_state*/)
{
  joint_name_ = get_node()->get_parameter("joint").as_string();

  if (joint_name_.empty())
  {
    RCLCPP_ERROR(get_node()->get_logger(), "'joint' parameter was empty");
    return CallbackReturn::ERROR;
  }

  // Specialized, child controllers set interfaces before calling configure function.
  if (interface_names_.empty())
  {
    interface_names_ = get_node()->get_parameter("interface_names").as_string_array();
  }

  if (interface_names_.empty())
  {
    RCLCPP_ERROR(get_node()->get_logger(), "'interface_names' parameter was empty");
    return CallbackReturn::ERROR;
  }

  twist_command_subscriber_ = get_node()->create_subscription<CmdType>(
    "~/commands", rclcpp::SystemDefaultsQoS(),
    [this](const CmdType::SharedPtr msg) { rt_command_ptr_.writeFromNonRT(msg); });

  twist_gripper_subscriber_ = get_node()->create_subscription<GripperVelType>(
    "~/gripper_vel", rclcpp::SystemDefaultsQoS(),
    [this](const GripperVelType::SharedPtr msg) { rt_gripper_ptr_.writeFromNonRT(msg); });

  RCLCPP_INFO(get_node()->get_logger(), "configure successful");
  return CallbackReturn::SUCCESS;
}

CallbackReturn PicknikTwistController::on_activate(
  const rclcpp_lifecycle::State & /*previous_state*/)
{
  // reset command buffer if a command came through callback when controller was inactive
  rt_command_ptr_ = realtime_tools::RealtimeBuffer<std::shared_ptr<CmdType>>(nullptr);
  return CallbackReturn::SUCCESS;
}

CallbackReturn PicknikTwistController::on_deactivate(
  const rclcpp_lifecycle::State & /*previous_state*/)
{
  // reset command buffer
  rt_command_ptr_ = realtime_tools::RealtimeBuffer<std::shared_ptr<CmdType>>(nullptr);
  return CallbackReturn::SUCCESS;
}

controller_interface::return_type PicknikTwistController::update(
  const rclcpp::Time & time, const rclcpp::Duration & /*period*/)
{
  auto twist_commands = rt_command_ptr_.readFromRT();
  auto gripper_command = rt_gripper_ptr_.readFromRT();

  // no command received yet
  if (!twist_commands || !(*twist_commands))
  {
    return controller_interface::return_type::OK;
  }

  if (command_interfaces_.size() != 7)
  {
    RCLCPP_ERROR_THROTTLE(
      get_node()->get_logger(), *get_node()->get_clock(), 1000,
      "Twist controller needs does not match number of interfaces needed 7, given (%zu) interfaces",
      command_interfaces_.size());
    return controller_interface::return_type::ERROR;
  }

  if (time - (*twist_commands)->header.stamp > rclcpp::Duration::from_seconds(0.4))
  {
    command_interfaces_[0].set_value(0);
    command_interfaces_[1].set_value(0);
    command_interfaces_[2].set_value(0);
    command_interfaces_[3].set_value(0);
    command_interfaces_[4].set_value(0);
    command_interfaces_[5].set_value(0);

    return controller_interface::return_type::OK;
  }

  command_interfaces_[0].set_value((*twist_commands)->twist.linear.x);
  command_interfaces_[1].set_value((*twist_commands)->twist.linear.y);
  command_interfaces_[2].set_value((*twist_commands)->twist.linear.z);
  command_interfaces_[3].set_value((*twist_commands)->twist.angular.x);
  command_interfaces_[4].set_value((*twist_commands)->twist.angular.y);
  command_interfaces_[5].set_value((*twist_commands)->twist.angular.z);

  // no gripper command received yet
  if (!gripper_command || !(*gripper_command))
  {
    command_interfaces_[6].set_value(0);
  }
  else
  {
    command_interfaces_[6].set_value((*gripper_command)->data);
  }

  return controller_interface::return_type::OK;
}
}  // namespace picknik_twist_controller

#include "pluginlib/class_list_macros.hpp"

PLUGINLIB_EXPORT_CLASS(
  picknik_twist_controller::PicknikTwistController, controller_interface::ControllerInterface)
