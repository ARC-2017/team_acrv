/*
Copyright 2016 Australian Centre for Robotic Vision

DEPRECATED

This node was created to save separate pcd files for each of the
labels generated by the segmentation_ros node. This functionality has now been
implemented in split_labelled_point_cloud_ros_node as a service call.
*/

#include <apc_3d_vision.hpp>

#include <ros/ros.h>
#include <image_transport/image_transport.h>
#include <tf/transform_listener.h>

#include <sensor_msgs/image_encodings.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/PointCloud2.h>

#include <cv_bridge/cv_bridge.h>
#include <pcl_ros/point_cloud.h>

#include <vector>
#include <utility>
#include <map>
#include "std_msgs/String.h"
#include "apc_msgs/EnablePublisher.h"
#include "apc_msgs/DoSegmentation.h"
#include "apc_3d_vision/split_labelled_point_cloud.h"
#include "apc_3d_vision/fit_cad_models.h"

boost::shared_ptr<pcl::PointCloud<pcl::PointXYZ>> raw_point_cloud;
boost::shared_ptr<pcl::PointCloud<pcl::PointXYZL>> segmented_point_cloud;
std::map<int, boost::shared_ptr<pcl::PointCloud<pcl::PointXYZL>>> object_map_l;
std::map<int, boost::shared_ptr<pcl::PointCloud<pcl::PointXYZ>>> object_map;
Apc3dVision apc_vis;

int main(int argc, char **argv) {
    raw_point_cloud.reset(new pcl::PointCloud<pcl::PointXYZ>);
    segmented_point_cloud.reset(new pcl::PointCloud<pcl::PointXYZL>);

    /**
    * The ros::init() function needs to see argc and argv so that it can perform
    * any ROS arguments and name remapping that were provided at the command line.
    * For programmatic remappings you can use a different version of init() which takes
    * remappings directly, but for most command-line programs, passing argc and argv is
    * the easiest way to do it.  The third argument to init() is the name of the node.
    *
    * You must call one of the versions of ros::init() before using any other
    * part of the ROS system.
    */
    ros::init(argc, argv, "listener");

    /**
    * NodeHandle is the main access point to communications with the ROS system.
    * The first NodeHandle constructed will fully initialize this node, and the last
    * NodeHandle destructed will close down the node.
    */
    ros::NodeHandle n;

    /**
    * The subscribe() call is how you tell ROS that you want to receive messages
    * on a given topic.  This invokes a call to the ROS
    * master node, which keeps a registry of who is publishing and who
    * is subscribing.  Messages are passed to a callback function, here
    * called chatterCallback.  subscribe() returns a Subscriber object that you
    * must hold on to until you want to unsubscribe.  When all copies of the Subscriber
    * object go out of scope, this callback will automatically be unsubscribed from
    * this topic.
    *
    * The second parameter to the subscribe() function is the size of the message
    * queue.  If messages are arriving faster than they are being processed, this
    * is the number of messages that will be buffered up before beginning to throw
    * away the oldest ones.
    */

    // Disable the segmentation_ros segmentation publisher
    ros::ServiceClient enable_client =
        n.serviceClient<apc_msgs::EnablePublisher>("enable_publisher");

    apc_msgs::EnablePublisher enable_srv;
    enable_srv.request.enable = false;

    if (enable_client.call(enable_srv)) {
        // Do nothing
    } else {
        ROS_ERROR("Failed to call service EnablePublisher");
        return 1;
    }

    // Call the segmentation_ros segmentation service
    ros::ServiceClient segment_client =
        n.serviceClient<apc_msgs::DoSegmentation>("do_segmentation");

    apc_msgs::DoSegmentation segment_srv;
    apc_vis.load_pcd_file("/home/baxter/co/apc_ws/cadModels/kinect_raw.pcd",
        raw_point_cloud);
    pcl::toROSMsg(*raw_point_cloud, segment_srv.request.input_cloud);

    if (segment_client.call(segment_srv)) {
        // pcl::fromROSMsg(segment_srv.response.segmented_cloud,
        //     *segmented_point_cloud);
    } else {
        ROS_ERROR("Failed to call service DoSegmentation");
        return 1;
    }

    // std::cout << segment_srv.response.segmented_cloud << std::cout;  // works

    // Call the split_labelled_point_cloud service
    ros::ServiceClient splitter_client =
        n.serviceClient<apc_3d_vision::split_labelled_point_cloud>(
            "/apc_3d_vision/split_labelled_point_cloud");

    apc_3d_vision::split_labelled_point_cloud splitter_srv;
    splitter_srv.request.labelled_points =
        segment_srv.response.segmented_cloud;
    // std::cout << splitter_srv.request.labelled_points << std::cout;  // works

    if (splitter_client.call(splitter_srv)) {
        // Do nothing
    } else {
        ROS_ERROR("Failed to call service split_labelled_point_cloud");
        return 1;
    }

    // Call the fit_cad_models service
    ros::ServiceClient fitter_client =
        n.serviceClient<apc_3d_vision::fit_cad_models>(
            "/apc_3d_vision/fit_cad_models");

    apc_3d_vision::fit_cad_models fitter_srv;
    std_msgs::String fileName;
    fileName.data = "/home/baxter/co/apc_ws/cadModels/cheeseits_10k.pcd";
    fitter_srv.request.candidate_cad_model_paths_array.push_back(fileName);

    int numberOfObjects = splitter_srv.response.labelled_points_array.size();
    std::cout << "There are " << numberOfObjects << " objects.\n";
    for (int i = 0; i < numberOfObjects; i++) {
        fitter_srv.request.labelled_points_array.push_back(
            splitter_srv.response.labelled_points_array[i]);
    }

    if (fitter_client.call(fitter_srv)) {
        // Do nothing
    } else {
        ROS_ERROR("Failed to call service fit_cad_models");
        return 1;
    }

    boost::shared_ptr<pcl::PointCloud<pcl::PointXYZ>> temp_point_cloud;
    temp_point_cloud.reset(new pcl::PointCloud<pcl::PointXYZ>);
    for (int i = 0; i < numberOfObjects; i++) {
        std::stringstream fileName;
        fileName << "/home/baxter/co/apc_ws/cadModels/output_pcds/"
            << i << ".pcd";
        pcl::fromROSMsg(fitter_srv.response.cad_model_points_array[i],
            *temp_point_cloud);
        if (!temp_point_cloud->empty()) {
            apc_vis.save_pcd_file(fileName.str(), temp_point_cloud);
        }
    }

    /**
    * ros::spin() will enter a loop, pumping callbacks.  With this version, all
    * callbacks will be called from within this thread (the main one).  ros::spin()
    * will exit when Ctrl-C is pressed, or the node is shutdown by the master.
    */

    // while (ros::ok()) {
    //     ros::spinOnce();
    // }

    return 0;
}
