/**
* This file is part of ORB-SLAM2.
*
* Copyright (C) 2014-2016 Raúl Mur-Artal <raulmur at unizar dot es> (University of Zaragoza)
* For more information see <https://github.com/raulmur/ORB_SLAM2>
*
* ORB-SLAM2 is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* ORB-SLAM2 is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with ORB-SLAM2. If not, see <http://www.gnu.org/licenses/>.
*/


#include<iostream>
#include<algorithm>
#include<fstream>
#include<chrono>

#include<ros/ros.h>
#include <cv_bridge/cv_bridge.h>
#include <message_filters/subscriber.h>
#include <message_filters/time_synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

#include<opencv2/core/core.hpp>

#include"../../../include/System.h"


#include <Converter.h>
#include <geometry_msgs/Pose.h>
#include <geometry_msgs/PoseStamped.h>
#include <tf/transform_broadcaster.h>
#include <tf/transform_datatypes.h>

using namespace std;

tf::Quaternion hamiltonProduct(tf::Quaternion a, tf::Quaternion b);

class ImageGrabber
{
public:
    ORB_SLAM2::System* mpSLAM;

    ros::Publisher kf_publisher;
    ros::Publisher kf_stamped_publisher;
    tf::TransformBroadcaster* br;    

public:
    ImageGrabber(ORB_SLAM2::System* pSLAM, ros::NodeHandle nh, tf::TransformBroadcaster* _br):mpSLAM(pSLAM){
        kf_publisher = nh.advertise<geometry_msgs::Pose>("/orb_slam/keyframe_optimized", 10);
        kf_stamped_publisher = nh.advertise<geometry_msgs::PoseStamped>("/orb_slam/keyframe_stamped_optimized", 10);
        br = _br;
    }

    void GrabRGBD(const sensor_msgs::ImageConstPtr& msgRGB,const sensor_msgs::ImageConstPtr& msgD);
};

int main(int argc, char **argv)
{
    ros::init(argc, argv, "RGBD");
    ros::start();
    ros::NodeHandle nh("~");
    tf::TransformBroadcaster br;

    // if(argc != 3)
     //{
       //  cerr << endl << "Usage: rosrun ORB_SLAM2 RGBD path_to_vocabulary path_to_settings" << endl;        
         //ros::shutdown();
         //return 1;
     //}    
    
    string strSettingsFile = "/home/natnael/git/subgit/ORB_SLAM2/Examples/ROS/ORB_SLAM2/Asus.yaml";
    string strVocFile = "/home/natnael/git/subgit/ORB_SLAM2/Vocabulary/ORBvoc.txt";

    string topic_rgb = "/camera/rgb/image_raw";
    string topic_depth = "/camera/depth/image";

    nh.param<std::string>("strSettingsFile", strSettingsFile, strSettingsFile);
    nh.param<std::string>("strVocFile", strVocFile, strVocFile);
    nh.param<std::string>("topic_rgb", topic_rgb, topic_rgb);
    nh.param<std::string>("topic_depth", topic_depth, topic_depth);

    // Create SLAM system. It initializes all system threads and gets ready to process frames.
    // ORB_SLAM2::System SLAM(argv[1],argv[2],ORB_SLAM2::System::RGBD,true);
    ORB_SLAM2::System SLAM(strVocFile, strSettingsFile, ORB_SLAM2::System::RGBD, false);

    ImageGrabber igb(&SLAM, nh, &br);

    message_filters::Subscriber<sensor_msgs::Image> rgb_sub(nh, topic_rgb, 1);
    message_filters::Subscriber<sensor_msgs::Image> depth_sub(nh, topic_depth, 1);
    typedef message_filters::sync_policies::ApproximateTime<sensor_msgs::Image, sensor_msgs::Image> sync_pol;
    message_filters::Synchronizer<sync_pol> sync(sync_pol(10), rgb_sub,depth_sub);
    sync.registerCallback(boost::bind(&ImageGrabber::GrabRGBD,&igb,_1,_2));

    ros::spin();

    // Stop all threads
    SLAM.Shutdown();

    // Save camera trajectory
    SLAM.SaveKeyFrameTrajectoryTUM("KeyFrameTrajectory.txt");

    ros::shutdown();

    return 0;
}

tf::Quaternion hamiltonProduct(tf::Quaternion a, tf::Quaternion b) 
{    
    tf::Quaternion c;

        c[0] = (a[0]*b[0]) - (a[1]*b[1]) - (a[2]*b[2]) - (a[3]*b[3]);
        c[1] = (a[0]*b[1]) + (a[1]*b[0]) + (a[2]*b[3]) - (a[3]*b[2]);
        c[2] = (a[0]*b[2]) - (a[1]*b[3]) + (a[2]*b[0]) + (a[3]*b[1]);
        c[3] = (a[0]*b[3]) + (a[1]*b[2]) - (a[2]*b[1]) + (a[3]*b[0]);

    return c;
}

void ImageGrabber::GrabRGBD(const sensor_msgs::ImageConstPtr& msgRGB,const sensor_msgs::ImageConstPtr& msgD)
{
    // Copy the ros image message to cv::Mat.
    cv_bridge::CvImageConstPtr cv_ptrRGB;
    try
    {
        cv_ptrRGB = cv_bridge::toCvShare(msgRGB);
    }
    catch (cv_bridge::Exception& e)
    {
        ROS_ERROR("cv_bridge exception: %s", e.what());
        return;
    }

    cv_bridge::CvImageConstPtr cv_ptrD;
    try
    {
        cv_ptrD = cv_bridge::toCvShare(msgD);
    }
    catch (cv_bridge::Exception& e)
    {
        ROS_ERROR("cv_bridge exception: %s", e.what());
        return;
    }

    cv::Mat pose = mpSLAM->TrackRGBD(cv_ptrRGB->image,cv_ptrD->image,cv_ptrRGB->header.stamp.toSec());

    if (pose.empty())    return;

    //Quaternion
    tf::Matrix3x3 tf3d;
    tf3d.setValue(pose.at<float>(0,0), pose.at<float>(0,1), pose.at<float>(0,2),
            pose.at<float>(1,0), pose.at<float>(1,1), pose.at<float>(1,2),
            pose.at<float>(2,0), pose.at<float>(2,1), pose.at<float>(2,2));

    tf::Quaternion tfqt;
    tf3d.getRotation(tfqt);
    double aux = tfqt[0];
        tfqt[0]=-tfqt[2];
        tfqt[2]=tfqt[1];
        tfqt[1]=aux;



    //Translation for camera
    tf::Vector3 origin;
    origin.setValue(pose.at<float>(0,3),pose.at<float>(1,3),pose.at<float>(2,3));
    //rotate 270deg about x and 270deg about x to get ENU: x forward, y left, z up
    const tf::Matrix3x3 rotation270degXZ(   0, 1, 0,
                                            0, 0, 1,
                                            -1, 0, 0);

    tf::Vector3 translationForCamera = origin * rotation270degXZ;

    //Hamilton (Translation for world)
    tf::Quaternion quaternionForHamilton(tfqt[3], tfqt[0], tfqt[1], tfqt[2]);
    tf::Quaternion secondQuaternionForHamilton(tfqt[3], -tfqt[0], -tfqt[1], -tfqt[2]);
    tf::Quaternion translationHamilton(0, translationForCamera[0], translationForCamera[1], translationForCamera[2]);

    tf::Quaternion translationStepQuat;
    translationStepQuat = hamiltonProduct(hamiltonProduct(quaternionForHamilton, translationHamilton), secondQuaternionForHamilton);

    tf::Vector3 translation(translationStepQuat[1], translationStepQuat[2], translationStepQuat[3]);

    

    //Creates transform and populates it with translation and quaternion
    tf::Transform transformCurrent;
    transformCurrent.setOrigin(translation);
    transformCurrent.setRotation(tfqt);

    br->sendTransform(tf::StampedTransform(transformCurrent, ros::Time::now(), "world", "orb_slam"));

    geometry_msgs::Pose kf_pose;
    tf::poseTFToMsg(transformCurrent, kf_pose);
    kf_publisher.publish(kf_pose);

    geometry_msgs::PoseStamped kf_pose_stamped;
    kf_pose_stamped.header.stamp = ros::Time::now();
    kf_pose_stamped.header.frame_id = "world";
    kf_pose_stamped.pose = kf_pose;
    kf_stamped_publisher.publish(kf_pose_stamped);
}
