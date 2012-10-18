/**
 * @file altitude_control_wrench_policy.cpp
 * @brief Wrench teleoperation policy implementation.
 * @author Stephan Wirth
 * @date 2012-10-16
 */

#include <string>
#include <std_msgs/Float32.h>
#include <auv_control/EnableControl.h>

#include "fugu_teleoperation/altitude_control_wrench_policy.h"
#include "control_common/control_types.h"

fugu_teleoperation::AltitudeControlWrenchPolicy::AltitudeControlWrenchPolicy(const ros::NodeHandle& n,
                                               const ros::NodeHandle& p)
{
  nh_ = n;
  priv_ = ros::NodeHandle(p,"altitude_control_wrench_policy");
}


/** Set parameters from parameter server.
 */
void fugu_teleoperation::AltitudeControlWrenchPolicy::initParams()
{
  ROS_INFO_STREAM("Setting wrench policy mapping and parameters...");
  std::string dof_names[NUM_DOFS];
  dof_names[LINEAR_X] = "linear_x";
  dof_names[LINEAR_Y] = "linear_y";
  dof_names[ALTITUDE] = "altitude";
  dof_names[ANGULAR_X] = "angular_x";
  dof_names[ANGULAR_Y] = "angular_y";
  dof_names[ANGULAR_Z] = "angular_z";
  for (int i=0; i<NUM_DOFS; i++)
  {
    std::string dof_name = dof_names[i];
    priv_.param(dof_name+"_axis",dof_map_[i].axis_,-1);
    priv_.param(dof_name+"_negative_offset_button",dof_map_[i].negative_offset_button_,-1);
    priv_.param(dof_name+"_positive_offset_button",dof_map_[i].positive_offset_button_,-1);
    priv_.param(dof_name+"_reset_button",dof_map_[i].reset_button_,-1);
    priv_.param(dof_name+"_factor", dof_map_[i].factor_,0.0);
    priv_.param(dof_name+"_offset_step", dof_map_[i].offset_step_,0.0);

    ROS_DEBUG_STREAM(dof_name+" axis   : " << dof_map_[i].axis_);
    ROS_DEBUG_STREAM(dof_name+" factor : " << dof_map_[i].factor_);
    ROS_DEBUG_STREAM(dof_name+" offset buttons (+/-): " << dof_map_[i].positive_offset_button_ << " "
                                                   << dof_map_[i].negative_offset_button_);
    ROS_DEBUG_STREAM(dof_name+" offset step    : " << dof_map_[i].offset_step_);
    ROS_DEBUG_STREAM(dof_name+" reset button : " << dof_map_[i].reset_button_);
  }
  priv_.param("pause_button",pause_button_,-1);
  ROS_DEBUG_STREAM("Pause button set to " << pause_button_);

  priv_.param("frame_id",frame_id_,std::string(""));
  ROS_DEBUG_STREAM("Frame id set to " << frame_id_);
}


void fugu_teleoperation::AltitudeControlWrenchPolicy::advertiseTopics()
{
  ROS_INFO_STREAM("Advertising teleoperation wrench request...");
  wrench_pub_ = priv_.advertise<geometry_msgs::WrenchStamped>("wrench_request", 10);
  bool latched = true;
  altitude_request_pub_ = priv_.advertise<std_msgs::Float32>("altitude_request", 1, latched);

}


void fugu_teleoperation::AltitudeControlWrenchPolicy::init()
{
  initParams();
  advertiseTopics();
}


/** TeleopPolicy start function redefinition.
 *
 * Reset wrench levels to null state.
 */
void fugu_teleoperation::AltitudeControlWrenchPolicy::start()
{
  ROS_INFO_STREAM("Initializing altitude control wrench policy states...");
  for (int i=0; i<NUM_DOFS; i++)
  {
    dof_state_[i].offset_ = 0.0;
    dof_state_[i].value_ = 0.0;
  }
  geometry_msgs::WrenchStamped msg;
  msg.header.stamp = ros::Time::now();
  msg.header.frame_id = frame_id_;
  msg.wrench.force.x = 0.0;
  msg.wrench.force.y = 0.0;
  msg.wrench.force.z = 0.0;
  msg.wrench.torque.x = 0.0;
  msg.wrench.torque.y = 0.0;
  msg.wrench.torque.z = 0.0;
  wrench_pub_.publish(msg);

  ros::ServiceClient client = nh_.serviceClient<auv_control::EnableControl>("enable_altitude_control");
  auv_control::EnableControl control_service;
  control_service.request.enable = true;
  if (client.call(control_service))
  {
    if (control_service.response.enabled)
    {
      ROS_INFO("Altitude control enabled.");
      dof_state_[ALTITUDE].offset_ = control_service.response.current_setpoint;
    }
    else
    {
      ROS_ERROR("Failed to enable altitude control!");
    }
  }
  else
  {
    ROS_ERROR("Failed to call service %s", nh_.resolveName("enable_altitude_control").c_str());
  }
}

bool fugu_teleoperation::AltitudeControlWrenchPolicy::updateDOFState(
    DOFState& d,
    const DOFMapping& m,
    const JoyState& j)
{
  bool update = false;
  if ( j.buttonPressed(m.reset_button_) )
  {
    d.offset_ = 0.0;
    update = true;
  }
  if ( j.buttonPressed(m.negative_offset_button_) )
  {
    d.offset_ -= m.offset_step_;
    update = true;
  }
  if ( j.buttonPressed(m.positive_offset_button_) )
  {
    d.offset_ += m.offset_step_;
    update = true;
  }
  if ( j.axisMoved(m.axis_) )
  {
    d.value_ = m.factor_*j.axisPosition(m.axis_);
    update = true;
  }
  return update;
}


/** TeleopPolicy update function redefinition.
 *
 * Implemented button actions are:
 *   - increase force/torque offset for each DOF
 *   - decrease force/torque offset for each DOF
 *   - reset force/torque offset for each DOF
 *   - set all DOFs to null state  (without resetting offsets)
 * If any DOF wrench level is modified by the joystick,
 * the DOF state is updated and a new wrench levels message is published.
 *
 * @param j joystick state.
 */
void fugu_teleoperation::AltitudeControlWrenchPolicy::update(const JoyState& j)
{
  bool updated = false;
  for (int i=0; i<NUM_DOFS; i++)
  {
    if (i != ALTITUDE)
    {
      if ( updateDOFState(dof_state_[i], dof_map_[i], j) )
        updated = true;
    }
  }
  bool altitude_updated = false;
  if ( updateDOFState(dof_state_[ALTITUDE], dof_map_[ALTITUDE], j) )
    altitude_updated = true;

  bool pause = j.buttonPressed(pause_button_);
  if ( pause )
  {
    control_common::WrenchLevelsStampedPtr msg(new control_common::WrenchLevelsStamped());
    msg->header.stamp = ros::Time(j.stamp());
    msg->header.frame_id = frame_id_;
    msg->wrench.force.x = 0.0;
    msg->wrench.force.y = 0.0;
    msg->wrench.force.z = 0.0;
    msg->wrench.torque.x = 0.0;
    msg->wrench.torque.y = 0.0;
    msg->wrench.torque.z = 0.0;
    wrench_pub_.publish(msg);
  }
  else if ( updated )
  {
    control_common::WrenchLevelsStampedPtr msg(new control_common::WrenchLevelsStamped());
    msg->header.stamp = ros::Time(j.stamp());
    msg->header.frame_id = frame_id_;
    msg->wrench.force.x = dof_state_[LINEAR_X].offset_ + dof_state_[LINEAR_X].value_;
    msg->wrench.force.y = dof_state_[LINEAR_Y].offset_ + dof_state_[LINEAR_Y].value_;
    msg->wrench.torque.x = dof_state_[ANGULAR_X].offset_ + dof_state_[ANGULAR_X].value_;
    msg->wrench.torque.y = dof_state_[ANGULAR_Y].offset_ + dof_state_[ANGULAR_Y].value_;
    msg->wrench.torque.z = dof_state_[ANGULAR_Z].offset_ + dof_state_[ANGULAR_Z].value_;
    wrench_pub_.publish(msg);
  }
  if ( altitude_updated )
  {
    std_msgs::Float32 altitude_request_msg;
    altitude_request_msg.data = dof_state_[ALTITUDE].offset_ + dof_state_[ALTITUDE].value_;
    altitude_request_pub_.publish(altitude_request_msg);
  }
}


/** TeleopPolicy stop function redefinition.
 *
 * Send a wrench levels message with null wrench for every DOF.
 */
void fugu_teleoperation::AltitudeControlWrenchPolicy::stop()
{
  ROS_INFO_STREAM("Sending null command on wrench policy stop...");
  control_common::WrenchLevelsStampedPtr msg(new control_common::WrenchLevelsStamped());
  msg->header.stamp = ros::Time::now();
  msg->header.frame_id = frame_id_;
  msg->wrench.force.x = 0.0;
  msg->wrench.force.y = 0.0;
  msg->wrench.force.z = 0.0;
  msg->wrench.torque.x = 0.0;
  msg->wrench.torque.y = 0.0;
  msg->wrench.torque.z = 0.0;
  wrench_pub_.publish(msg);

  ros::ServiceClient client = nh_.serviceClient<auv_control::EnableControl>("enable_altitude_control");
  auv_control::EnableControl control_service;
  control_service.request.enable = false;
  if (client.call(control_service))
  {
    if (!control_service.response.enabled)
    {
      ROS_INFO("Altitude control disabled.");
    }
    else
    {
      ROS_ERROR("Failed to disable altitude control!");
    }
  }
  else
  {
    ROS_ERROR("Failed to call service %s", nh_.resolveName("enable_altitude_control").c_str());
  }
}

