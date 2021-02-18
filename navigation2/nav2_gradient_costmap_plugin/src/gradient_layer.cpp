/*********************************************************************
 *
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2008, 2013, Willow Garage, Inc.
 *  Copyright (c) 2020, Samsung R&D Institute Russia
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Eitan Marder-Eppstein
 *         David V. Lu!!
 *         Alexey Merzlyakov
 *
 * Reference tutorial:
 * https://navigation.ros.org/tutorials/docs/writing_new_costmap2d_plugin.html
 *********************************************************************/
#include "nav2_gradient_costmap_plugin/gradient_layer.hpp"

#include "nav2_costmap_2d/costmap_math.hpp"
#include "nav2_costmap_2d/footprint.hpp"
#include "rclcpp/parameter_events_filter.hpp"

using nav2_costmap_2d::LETHAL_OBSTACLE;
using nav2_costmap_2d::INSCRIBED_INFLATED_OBSTACLE;
using nav2_costmap_2d::NO_INFORMATION;
using nav2_costmap_2d::FREE_SPACE;

namespace nav2_gradient_costmap_plugin
{

GradientLayer::GradientLayer()
{
}

// This method is called at the end of plugin initialization.
// It contains ROS parameter(s) declaration and initialization
// of need_recalculation_ variable.
void
GradientLayer::onInitialize()
{
  getParameters();
  matchSize();

  overhead_camera_1_ = std::make_shared<overhead_camera::overhead_camera>(cam1_x_,cam1_y_,cam1_z_);
  overhead_camera_2_ = std::make_shared<overhead_camera::overhead_camera>(cam2_x_,cam2_y_,cam2_z_);

  cam_sub_1_ = node_->create_subscription<sensor_msgs::msg::Image>(cam1_topic_, rclcpp::SystemDefaultsQoS(),
                                                                   std::bind(&overhead_camera::overhead_camera::image_cb, overhead_camera_1_, std::placeholders::_1));
  cam_sub_2_ = node_->create_subscription<sensor_msgs::msg::Image>(cam2_topic_, rclcpp::SystemDefaultsQoS(),
                                                                   std::bind(&overhead_camera::overhead_camera::image_cb, overhead_camera_2_, std::placeholders::_1));
  calDesiredSize();
  RCLCPP_INFO(node_->get_logger(), "subscribing to %s \n %s", cam1_topic_.c_str(), cam2_topic_.c_str());
}

void GradientLayer::getParameters() {
  declareParameter("enabled", rclcpp::ParameterValue(true));
  declareParameter("cam1_topic", rclcpp::ParameterValue(""));
  declareParameter("cam2_topic", rclcpp::ParameterValue(""));
  declareParameter("cam1_x", rclcpp::ParameterValue(0.0));
  declareParameter("cam1_y", rclcpp::ParameterValue(0.0));
  declareParameter("cam1_z", rclcpp::ParameterValue(0.0));
  declareParameter("cam2_x", rclcpp::ParameterValue(0.0));
  declareParameter("cam2_y", rclcpp::ParameterValue(0.0));
  declareParameter("cam2_z", rclcpp::ParameterValue(0.0));

  node_->get_parameter(name_ + "." + "enabled", enabled_);
  node_->get_parameter(name_ + "." + "cam1_topic", cam1_topic_);
  node_->get_parameter(name_ + "." + "cam2_topic", cam2_topic_);

  node_->get_parameter(name_+ "." + "cam1_x",cam1_x_);
  node_->get_parameter(name_+ "." + "cam1_y",cam1_y_);
  node_->get_parameter(name_+ "." + "cam1_z",cam1_z_);
  node_->get_parameter(name_+ "." + "cam2_x",cam2_x_);
  node_->get_parameter(name_+ "." + "cam2_y",cam2_y_);
  node_->get_parameter(name_+ "." + "cam2_z",cam2_z_);

}

void nav2_gradient_costmap_plugin::GradientLayer::calDesiredSize() {
  double x1_min, y1_min, x1_max, y1_max;
  double x2_min, y2_min, x2_max, y2_max;
  overhead_camera_1_->worldFOV(x1_min, y1_min, x1_max, y1_max);
  overhead_camera_2_->worldFOV(x2_min, y2_min, x2_max, y2_max);

  double x_union_min = fmin(x1_min,x2_min);
  x_union_min = fmin(x_union_min, static_cast<double>(getOriginX()));
  double y_union_min = fmin(y1_min, y2_min);
  y_union_min = fmin(y_union_min, static_cast<double>(getOriginY()));

  //TODO must be implemented as upper block but is not possible without changing origin
  double x_union_max = fmax(x1_max, x2_max);
  double y_union_max = fmax(y1_max, y2_max);

  ceiling_size_x_ = static_cast<unsigned int>(fabs(x_union_max - x_union_min)/resolution_);
  ceiling_size_y_ = static_cast<unsigned int>(fabs(y_union_max - y_union_min)/resolution_);

}

// The method is called to ask the plugin: which area of costmap it needs to update.
// Inside this method window bounds are re-calculated if need_recalculation_ is true
// and updated independently on its value.
void
GradientLayer::updateBounds(
  double /*robot_x*/, double /*robot_y*/, double /*robot_yaw*/, double * min_x,
  double * min_y, double * max_x, double * max_y)
{

  if ( (size_x_ < ceiling_size_x_) || (size_y_ <ceiling_size_y_)) {
    Costmap2D * master = layered_costmap_->getCostmap();
    layered_costmap_->resizeMap(fmax(ceiling_size_x_, size_x_), fmax(ceiling_size_y_, size_y_),
                                master->getResolution(), master->getOriginX(), master->getOriginY(),
                                true); // origin is symmetric cause camera is at 0,0
    RCLCPP_INFO(node_->get_logger(), "GradientLayer: master_grid size after resizing: %d X %d",
                master->getSizeInCellsX(),
                master->getSizeInCellsY());

    unsigned int index = 0;
    // initialize the costmap with static data
    for (unsigned int i = 0; i < size_y_; ++i) {
      for (unsigned int j = 0; j < size_x_; ++j) {
        costmap_[index] = NO_INFORMATION;
        ++index;
      }
    }
  }


}

// The method is called when footprint was changed.
// Here it just resets need_recalculation_ variable.
void
GradientLayer::onFootprintChanged()
{
  RCLCPP_DEBUG(rclcpp::get_logger(
      "nav2_costmap_2d"), "GradientLayer::onFootprintChanged(): num footprint points: %lu",
    layered_costmap_->getFootprint().size());
}

// The method is called when costmap recalculation is required.
// It updates the costmap within its window bounds.
// Inside this method the costmap gradient is generated and is writing directly
// to the resulting costmap master_grid without any merging with previous layers.
void
GradientLayer::updateCosts(
  nav2_costmap_2d::Costmap2D & master_grid, int min_i, int min_j,
  int max_i,
  int max_j)
{
  if(overhead_camera_1_->isUpdate()){
    for(unsigned int j=0; j<size_y_; j++){
      for(unsigned int i=0; i<size_x_; i++){
        double wx, wy;
        mapToWorld(i, j, wx, wy);
        if(overhead_camera_1_->coverWorld(wx, wy)){
          unsigned int px, py;
          if(overhead_camera_1_->worldToPixel(wx, wy, px, py)){
            auto isFree = overhead_camera_1_->isGridFree(px, py);
            costmap_[master_grid.getIndex(i,j)] = (isFree ? FREE_SPACE : LETHAL_OBSTACLE);
          }else{
            RCLCPP_WARN(node_->get_logger(), "GradientLayer: worldToPixel was not successful");
          }
        }else{
          costmap_[master_grid.getIndex(i,j)] = NO_INFORMATION;
        }
      }
    }

    overhead_camera_1_->setUpdate(false);
    unsigned int index = 0;
    unsigned char * master_ = master_grid.getCharMap();
    // initialize the costmap with static data
    for (unsigned int i = 0; i < size_y_; ++i) {
      for (unsigned int j = 0; j < size_x_; ++j) {
        if(master_[index] == NO_INFORMATION && costmap_[index]!=NO_INFORMATION)
          master_[index] = costmap_[index];
        ++index;
      }
    }
  }
  //updateWithOverwrite(master_grid, 0,0,size_x_, size_y_);



}

}  // namespace nav2_gradient_costmap_plugin

// This is the macro allowing a nav2_gradient_costmap_plugin::GradientLayer class
// to be registered in order to be dynamically loadable of base type nav2_costmap_2d::Layer.
// Usually places in the end of cpp-file where the loadable class written.
#include "pluginlib/class_list_macros.hpp"
PLUGINLIB_EXPORT_CLASS(nav2_gradient_costmap_plugin::GradientLayer, nav2_costmap_2d::Layer)
