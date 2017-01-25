#include <odroid/odroid_node.hpp>
// #include <odroid/controllers.h>// robotic control
// User header files

using namespace std;
using namespace Eigen;
using namespace message_filters;

odroid_node::odroid_node(){
  del_t = 0.01;
  // m = 1.25;
  g = 9.81;
  // J <<  55710.50413e-7, 617.6577e-7, -250.2846e-7,
  // 617.6577e-7,  55757.4605e-7, 100.6760e-7,
  // -250.2846e-7, 100.6760e-7, 105053.7595e-7;// kg*m^2

  m = 0.755;
  J << 0.005571050413, 0.0           , 0.0          ,
       0.0           , 0.005571050413, 0.0          ,
       0.0           , 0.0           , 0.01050537595;

  IMU_flag = false;
  // f = VectorXd::Zero(6);
  R_bm <<  1.0, 0.0, 0.0,
  0.0, -1.0, 0.0,
  0.0, 0.0, -1.0;// Markers frame (m) to the body frame (b) (fixed)
  R_ev <<  1.0, 0.0, 0.0,
  0.0, -1.0, 0.0,
  0.0, 0.0, -1.0;// Vicon frame (v) to inertial frame (e) (fixed)
  eiX_last = VectorXd::Zero(3);
  eiR_last = VectorXd::Zero(3);
	x_e = VectorXd::Zero(3);

  // quat_vm = new VectorXd::Zeros(4);
  // Given the UAV arm length of 0.31 m and a prop. angle of 15 deg.
  invFMmat <<  0.0000,    1.2879,   -0.1725,   -0.0000,    1.1132,    0.3071,
  -1.1154,    0.6440,    0.1725,    0.9641,   -0.3420,    0.7579,
  -1.1154,   -0.6440,   -0.1725,   -0.9641,   -0.7712,    1.7092,
  -0.0000,   -1.2879,    0.1725,         0,    1.1132,    0.3071,
  1.1154,   -0.6440,   -0.1725,    0.9641,   -0.3420,    0.7579,
  1.1154,    0.6440,    0.1725,   -0.9641,   -0.7712,    1.7092;

  pub_ = n_.advertise<std_msgs::String>("/motor_command",1);
  vis_pub_ = n_.advertise<visualization_msgs::Marker>("/force",1);
  vis_pub_y = n_.advertise<visualization_msgs::Marker>("/force_y",1);
  vis_pub_z = n_.advertise<visualization_msgs::Marker>("/force_z",1);

  ROS_INFO("Odroid node initialized");
}
odroid_node::~odroid_node(){};

void odroid_node::print_J(){
  std::cout<<"J: \n"<<J<<std::endl;
}

void odroid_node::print_force(){
  std::cout<<"force: "<<f_quad<<std::endl;
}

// callback for IMU sensor det
bool odroid_node::getIMU(){return IMU_flag;}

void odroid_node::imu_callback(const sensor_msgs::Imu::ConstPtr& msg){
  W_raw(0) = msg->angular_velocity.x;
  W_raw(1) = msg->angular_velocity.y;
  W_raw(2) = msg->angular_velocity.z;
  W_b = W_raw;

  if(!IMU_flag){ ROS_INFO("IMU ready");}
  IMU_flag = true;
  // if(isnan(W_raw(0)) || isnan(W_raw(1)) || isnan(W_raw(2))){IMU_flag = false;}

  if(print_imu){
   printf("IMU: Psi:[%f], Theta:[%f], Phi:[%f] \n", W_raw(0), W_raw(1), W_raw(2));
  }
}

void odroid_node::imu_vicon_callback(const sensor_msgs::Imu::ConstPtr& msgImu, const geometry_msgs::TransformStamped::ConstPtr& msgVicon){
  W_raw(0) = msgImu->angular_velocity.x;
  W_raw(1) = msgImu->angular_velocity.y;
  W_raw(2) = msgImu->angular_velocity.z;
  W_b = W_raw;
  if(!IMU_flag){ ROS_INFO("IMU ready");}
  IMU_flag = true;
  // if(isnan(W_raw(0)) || isnan(W_raw(1)) || isnan(W_raw(2))){IMU_flag = false;}

  if(print_imu){
   printf("IMU: Psi:[%f], Theta:[%f], Phi:[%f] \n", W_raw(0), W_raw(1), W_raw(2));
  }

  x_v(0) = msgVicon->transform.translation.x;
  x_v(1) = msgVicon->transform.translation.y;
  x_v(2) = msgVicon->transform.translation.z;
  quat_vm(0) = msgVicon->transform.rotation.x;
  quat_vm(1) = msgVicon->transform.rotation.y;
  quat_vm(2) = msgVicon->transform.rotation.z;
  quat_vm(3) = msgVicon->transform.rotation.w;


  tf::Quaternion q(quat_vm(0),quat_vm(1),quat_vm(2),quat_vm(3));
  tf::Matrix3x3 m(q);

  double qx = quat_vm(0);
  double qy = quat_vm(1);
  double qz = quat_vm(2);
  double qw = quat_vm(3);

  R_v(0,0) = 1.0-2*qy*qy-2*qz*qz; R_v(0,1) = 2*qx*qy-2*qz*qw;     R_v(0,2) = 2*qx*qz+2*qy*qw;
  R_v(1,0) = 2*qx*qy+2*qz*qw;     R_v(1,1) = 1.0-2*qx*qx-2*qz*qz; R_v(1,2) = 2*qy*qz-2*qx*qw;
  R_v(2,0) = 2*qx*qz-2*qy*qw;     R_v(2,1) = 2*qy*qz+2*qx*qw;     R_v(2,2) = 1.0-2*qx*qx-2*qy*qy;


  m.getRPY(roll, pitch, yaw);

	if(print_vicon){
    printf("Vicon: xyz:[%f, %f, %f] roll:[%f], pitch:[%f], yaw:[%f] \n", x_v(0),x_v(1),x_v(2), roll/M_PI*180, pitch/M_PI*180, yaw/M_PI*180);
  }
  if(print_x_v){
    cout<<"x_v: "<<x_v.transpose()<<endl;
  }

  static tf::TransformBroadcaster br;
  tf::Transform transform;
  transform.setOrigin( tf::Vector3(x_v(0),x_v(1), x_v(2)));
  transform.setRotation(q);
  br.sendTransform(tf::StampedTransform(transform, vicon_time, "vicon", "base_link"));
  br.sendTransform(tf::StampedTransform(transform, vicon_time, "vicon", "imu"));

  Vector3d temp = M;
  temp = temp.normalized();
  tf::Quaternion q1;
  q1.setEuler(temp(0),temp(1),temp(2));
  transform.setRotation(q1);
  br.sendTransform(tf::StampedTransform(transform, vicon_time, "vicon", "moment"));


}

// vicon information callback
void odroid_node::vicon_callback(const geometry_msgs::TransformStamped::ConstPtr& msg){
  vicon_time = msg->header.stamp;

  x_v(0) = msg->transform.translation.x;
  x_v(1) = msg->transform.translation.y;
  x_v(2) = msg->transform.translation.z;
  quat_vm(0) = msg->transform.rotation.x;
  quat_vm(1) = msg->transform.rotation.y;
  quat_vm(2) = msg->transform.rotation.z;
  quat_vm(3) = msg->transform.rotation.w;

  tf::Quaternion q(quat_vm(0),quat_vm(1),quat_vm(2),quat_vm(3));
  tf::Matrix3x3 m(q);
  m.getRPY(roll, pitch, yaw);

	if(print_vicon){
    printf("Vicon: roll:[%f], pitch:[%f], yaw:[%f] \n", roll/M_PI*180, pitch/M_PI*180, yaw/M_PI*180);
  }
  if(print_x_v){
    cout<<"x_v: "<<x_v.transpose()<<endl;
  }
  static tf::TransformBroadcaster br;
  tf::Transform transform;
  transform.setOrigin( tf::Vector3(x_v(0),x_v(1), x_v(2)));
  transform.setRotation(q);
  br.sendTransform(tf::StampedTransform(transform, vicon_time, "vicon", "base_link"));
  br.sendTransform(tf::StampedTransform(transform, vicon_time, "vicon", "imu"));

  Vector3d temp = M;
  temp = temp.normalized();
  tf::Quaternion q1;
  q1.setEuler(temp(0),temp(1),temp(2));
  transform.setRotation(q1);
  br.sendTransform(tf::StampedTransform(transform, vicon_time, "vicon", "moment"));

}

// callback for key Inputs
void odroid_node::key_callback(const std_msgs::String::ConstPtr&  msg){
  std::cout<<*msg<<std::endl;
  // TODO: case function here for changing parameter dynamically. Or just make ros dynamic reconfigure file.
}
// Action for controller
void odroid_node::ctl_callback(){
  VectorXd Wd, Wd_dot;
  Wd = VectorXd::Zero(3); Wd_dot = VectorXd::Zero(3);
  //for attitude testing of position controller
  Vector3d xd_dot, xd_ddot;
  Matrix3d Rd;

  if(!(MOTOR_ON && !MotorWarmup)){
    kiR = 0;
    kiX = 0;
  }

  // xd = VectorXd::Zero(3);
  xd_dot = VectorXd::Zero(3); xd_ddot = VectorXd::Zero(3);
  Rd = MatrixXd::Identity(3,3);
  Vector3d prev_x_e = x_e;
  // Vector3d prev_x_v = x_v;
  x_e = R_ev * x_v;
  v_e = (x_e - prev_x_e)*100;

  v_v = (x_v - prev_x_v)*100;
  // cout<<"x_v\n"<<x_v.tanspose()<<"v_v\n"<<v_v.transpose()<<endl;
  prev_x_v = x_v;




  // cout<<"quat\n"<<quat_vm.transpose()<<endl;
  R_vm(0,0) = 1-(2*(quat_vm[1])*(quat_vm[1]))-(2*(quat_vm[2])*(quat_vm[2]));
  R_vm(0,1) = (2*quat_vm[0]*quat_vm[1])-(2*quat_vm[3]*quat_vm[2]);
  R_vm(0,2) = (2*quat_vm[0]*quat_vm[2])+(2*quat_vm[3]*quat_vm[1]);
  R_vm(1,0) = (2*quat_vm[0]*quat_vm[1])+(2*quat_vm[3]*quat_vm[2]);
  R_vm(1,1) = 1-(2*(quat_vm[0])*(quat_vm[0]))-(2*(quat_vm[2])*(quat_vm[2]));
  R_vm(1,2) = (2*(quat_vm[1])*(quat_vm[2]))-(2*(quat_vm[3])*(quat_vm[0]));
  R_vm(2,0) = (2*quat_vm[0]*quat_vm[2])-(2*quat_vm[3]*quat_vm[1]);
  R_vm(2,1) = (2*quat_vm[0]*quat_vm[3])+(2*quat_vm[2]*quat_vm[1]);
  R_vm(2,2) = 1-(2*(quat_vm[0])*(quat_vm[0]))-(2*(quat_vm[1])*(quat_vm[1]));
  // cout<<"R_vm\n"<<R_vm<<endl;
  // W_b << 0,0.0,0.5;
  // psi = 30/180*M_PI; //msg->orientation.x;
  // theta = 30/180*M_PI;//msg->orientation.y;
  // phi = 30/180*M_PI; //msg->orientation.z;
  // //cout<<"psi: "<<psi<<" theta: "<<theta<<" phi: "<<phi<<endl;
  // Eigen::Vector3d angle(roll,pitch,yaw);
  //euler_Rvm(R_vm, angle);

  R_eb = R_ev * R_vm * R_bm;
  if(print_R_eb){cout<<"R_eb: "<<R_eb<<endl;}
  //cout<<"R_eb\n"<<R_eb<<endl;
//cout<<"yaw"<<atan2(-R_eb(2,0),R_eb(0,0))<<endl;
//cout<<"roll"<<atan2(-R_eb(1,2),R_eb(1,1))<<endl;
//cout<<"pitch"<<asin(R_eb(1,0))<<endl;

  // R_eb = R_vm.transpose();
  del_t_CADS = 0.01;

  if(print_xd){
    cout<<"xd: "<<xd.transpose()<<endl;
  }


  //GeometricControl_SphericalJoint_3DOF_eigen(Wd, Wd_dot, W_b, R_eb, del_t_CADS, eiR);

QuadGeometricPositionController(xd, xd_dot, xd_ddot, Wd, Wd_dot, x_v, v_v, W_b, R_v);
// GeometricController_6DOF(xd, xd_dot, xd_ddot, Rd, Wd, Wd_dot, x_e, v_e, W_b, R_eb);


  // OutputMotor(f_quad,thr);
  // if(print_thr){
  //   cout<<"Throttle motor out: ";
  //   for(int i = 0;i<6;i++){
  //     cout<<thr[i]<<", ";} cout<<endl;
  //   }

  visualization_msgs::Marker marker;
  marker.header.frame_id = "base_link";
  marker.header.stamp = vicon_time;
  marker.ns = "odroid";
  marker.id = 0;
  marker.type = visualization_msgs::Marker::ARROW;
  marker.action = visualization_msgs::Marker::ADD;
  marker.pose.position.x = 0;
  marker.pose.position.y = 0;
  marker.pose.position.z = 0;
  marker.pose.orientation.x = 0;
  marker.pose.orientation.y = 0;
  marker.pose.orientation.z = 0;
  marker.pose.orientation.w = 1;
  marker.scale.x = M(0);
  marker.scale.y = 0.05;
  marker.scale.z = 0.05;
  marker.color.a = 1.0; // Don't forget to set the alpha!
  marker.color.r = 1.0;
  marker.color.g = 0.0;
  marker.color.b = 0.0;
  //only if using a MESH_RESOURCE marker type:
  // marker.mesh_resource = "package://pr2_description/meshes/base_v0/base.dae";
  vis_pub_.publish( marker);

  marker.pose.orientation.x = 0;
  marker.pose.orientation.y = 0;
  marker.pose.orientation.z = 0.707;
  marker.pose.orientation.w = 0.707;
  marker.scale.x = M(1);
  marker.color.r = 0.0;
  marker.color.g = 1.0;
  marker.color.b = 0.0;

  vis_pub_y.publish( marker);

  marker.pose.orientation.x = 0;
  marker.pose.orientation.y = 0.707;
  marker.pose.orientation.z = 0.0;
  marker.pose.orientation.w = 0.707;
  marker.scale.x = 0.5 * (f_quad - m*g);
  marker.color.r = 0.0;
  marker.color.g = 0.0;
  marker.color.b = 1.0;

  vis_pub_z.publish( marker);



  // motor_command();
}

void odroid_node::QuadGeometricPositionController(Vector3d xd, Vector3d xd_dot, Vector3d xd_ddot,Vector3d Wd, Vector3d Wddot, Vector3d x_v, Vector3d v_v, Vector3d W_in, Matrix3d R_v){

    // Bring to controller frame (and back) with 180 degree rotation about b1
    Matrix3d D = R_bm;
    // std::cout<<"R:\n"<<R_v<<std::endl;

    Vector3d xd_2dot = Vector3d::Zero();
    Vector3d xd_3dot = Vector3d::Zero();
    Vector3d xd_4dot = Vector3d::Zero();
    Vector3d b1d(1,0,0);
    Vector3d b1d_dot = Vector3d::Zero();
    Vector3d b1d_ddot = Vector3d::Zero();

    Vector3d e3(0.0,0.0,1.0);// commonly-used unit vector

    Vector3d x = D*x_v;// LI
    Vector3d v = D*v_v;// LI
    Matrix3d R = D*R_v*D;// LI<-LBFF
    Vector3d W = D*W_in;// LBFF

    // std::cout<<"x_v:\n"<<x_v.transpose()<<std::endl;
    // std::cout<<"x:\n"<<x.transpose()<<std::endl;
    // std::cout<<"v:\n"<<v.transpose()<<std::endl;
    // std::cout<<"R_v:\n"<<R_v<<std::endl;
    // std::cout<<"W:\n"<<W<<std::endl;

    xd = D*xd;
    xd_dot = D*xd_dot;
    xd_2dot = D*xd_2dot;
    xd_3dot = D*xd_3dot;
    xd_4dot = D*xd_4dot;

    // std::cout<<"xd:\n"<<xd.transpose()<<std::endl;

    b1d = D*b1d;
    b1d_dot = D*b1d_dot;
    b1d_ddot = D*b1d_ddot;

    // Saturation Limits
    double eiX_sat = 0.1;
    double eiR_sat = 0.01;
    // Translational Error Functions
    Vector3d ex = x - xd;
    Vector3d ev = v - xd_dot;
    Vector3d eiX = eiX+del_t*(ex+cX*ev);
    err_sat(-eiX_sat, eiX_sat, eiX);

      if(print_eX){cout<<"eX: "<<ex.transpose()<<endl;}
      if(print_eV){cout<<"eV: "<<ev.transpose()<<endl;}
    // std::cout<<"ex:\n"<<ex.transpose()<<std::endl;
    // std::cout<<"ev:\n"<<ev.transpose()<<std::endl;
    // std::cout<<"eiX:\n"<<eiX.transpose()<<std::endl;



    // Force 'f' along negative b3-axis
    Vector3d A = -kx*ex-kv*ev-kiX*eiX-m*g*e3+m*xd_2dot;
    Vector3d L = R*e3;
    Vector3d Ldot = R*hat_eigen(W)*e3;
    double f = -A.dot(R*e3);
    // std::cout<<f<<std::endl;
    f_quad = f;

    if(print_f){print_force();}
    // Intermediate Terms for Rotational Errors
    Vector3d ea = g*e3-f/m*L-xd_2dot;
    Vector3d Adot = -kx*ev-kv*ea+m*xd_3dot;// Lee Matlab: -ki*satdot(sigma,ei,ev+c1*ex);

    double fdot = -Adot.dot(L)-A.dot(Ldot);
    Vector3d eb = -fdot/m*L-f/m*Ldot-xd_3dot;
    Vector3d Addot = -kx*ea-kv*eb+m*xd_4dot;// Lee Matlab: -ki*satdot(sigma,ei,ea+c1*ev);

    double nA = A.norm();
    Vector3d Ld = -A/nA;
    Vector3d Lddot = -Adot/nA+A*A.dot(Adot)/pow(nA,3);
    Vector3d Ldddot = -Addot/nA+Adot/pow(nA,3)*(2*A.dot(Adot))
            +A/pow(nA,3)*(Adot.dot(Adot)+A.dot(Addot))
            -3*A/pow(nA,5)*pow(A.dot(Adot),2);

    Vector3d Ld2 = -hat_eigen(b1d)*Ld;
    Vector3d Ld2dot = -hat_eigen(b1d_dot)*Ld-hat_eigen(b1d)*Lddot;
    Vector3d Ld2ddot = -hat_eigen(b1d_ddot)*Ld-2*hat_eigen(b1d_dot)*Lddot-hat_eigen(b1d)*Ldddot;

    double nLd2 = Ld2.norm();
    Vector3d Rd2 = Ld2/nLd2;
    Vector3d Rd2dot = Ld2dot/nLd2-Ld2.dot(Ld2dot)/pow(nLd2,3)*Ld2;
    Vector3d Rd2ddot = Ld2ddot/nLd2-Ld2.dot(Ld2dot)/pow(nLd2,3)*Ld2dot
            -Ld2dot.dot(Ld2dot)/pow(nLd2,3)*Ld2-Ld2.dot(Ld2ddot)/pow(nLd2,3)*Ld2
            -Ld2.dot(Ld2dot)/pow(nLd2,3)*Ld2dot
            +3*pow(Ld2.dot(Ld2dot),2)/pow(nLd2,5)*Ld2;

    Vector3d Rd1 = hat_eigen(Rd2)*Ld;
    Vector3d Rd1dot = hat_eigen(Rd2dot)*Ld+hat_eigen(Rd2)*Lddot;
    Vector3d Rd1ddot = hat_eigen(Rd2ddot)*Ld+2*hat_eigen(Rd2dot)*Lddot+hat_eigen(Rd2)*Ldddot;

    Matrix3d Rd, Rddot, Rdddot;
    Rd << Rd1, Rd2, Ld;
    Rddot << Rd1dot, Rd2dot, Lddot;
    Rdddot << Rd1ddot, Rd2ddot, Ldddot;

    // Vector3d Wd, Wddot;
    vee_eigen(Rd.transpose()*Rddot, Wd);
    vee_eigen(Rd.transpose()*Rdddot-hat_eigen(Wd)*hat_eigen(Wd), Wddot);

    // Attitude Error 'eR'
    vee_eigen(.5*(Rd.transpose()*R-R.transpose()*Rd), eR);
    // cout<<"eR:\n"<<eR<<endl;
    // Angular Velocity Error 'eW'
    eW = W-R.transpose()*Rd*Wd;
    // cout<<"eW:\n"<<eW<<endl;
    // Attitude Integral Term
    eiR += del_t*(eR+cR*eW);
    err_sat(-eiR_sat, eiR_sat, eiR);

    // 3D Moment
    M = -kR*eR-kW*eW-kiR*eiR+hat_eigen(R.transpose()*Rd*Wd)*J*R.transpose()*Rd*Wd+J*R.transpose()*Rd*Wddot;// LBFF
    M = D*M;// LBFF->GBFF
    if(print_M){cout<<"M: "<<M.transpose()<<endl;}
}

void odroid_node::gazebo_controll(){
  ros::ServiceClient client_FM = n_.serviceClient<gazebo_msgs::ApplyBodyWrench>("/gazebo/apply_body_wrench");
  gazebo_msgs::ApplyBodyWrench FMcmds_srv;

  Vector3d fvec_GB(0.0, 0.0, f_quad), fvec_GI;

  fvec_GI = R_v*fvec_GB;
  Vector3d M_out = R_v*M;

  FMcmds_srv.request.body_name = "quadrotor::base_link";
  FMcmds_srv.request.reference_frame = "world";
  FMcmds_srv.request.reference_point.x = 0.0;
  FMcmds_srv.request.reference_point.y = 0.0;
  FMcmds_srv.request.reference_point.z = 0.0;
  FMcmds_srv.request.start_time = ros::Time(0.0);
  FMcmds_srv.request.duration = ros::Duration(0.01);// apply continuously until new command

  FMcmds_srv.request.wrench.force.x = fvec_GI(0);
  FMcmds_srv.request.wrench.force.y = fvec_GI(1);
  FMcmds_srv.request.wrench.force.z = fvec_GI(2);

  FMcmds_srv.request.wrench.torque.x = M_out(0);
  FMcmds_srv.request.wrench.torque.y = M_out(1);
  FMcmds_srv.request.wrench.torque.z = M_out(2);

  client_FM.call(FMcmds_srv);
  if(!FMcmds_srv.response.success)
      cout << "Fail! Response message:\n" << FMcmds_srv.response.status_message << endl;
}

int main(int argc, char **argv){
  // ros::init(argc, argv, "imu_listener");
  ros::init(argc,argv,"hexacopter");
  //  ros::NodeHandle nh;
  odroid_node odnode;
  ros::NodeHandle nh = odnode.getNH();
  // IMU and keyboard input callback
  // ros::Subscriber sub2 = nh.subscribe("imu/imu",100,&odroid_node::imu_callback,&odnode);
  // ros::Subscriber sub_vicon = nh.subscribe("vicon/Maya/Maya",100,&odroid_node::vicon_callback,&odnode);
  ros::Subscriber sub_key = nh.subscribe("cmd_key", 100, &odroid_node::key_callback, &odnode);

  // dynamic reconfiguration server for gains and print outs
  dynamic_reconfigure::Server<odroid::GainsConfig> server;
  dynamic_reconfigure::Server<odroid::GainsConfig>::CallbackType dyn_serv;
  dyn_serv = boost::bind(&odroid_node::callback, &odnode, _1, _2);
  server.setCallback(dyn_serv);

 typedef sync_policies::ApproximateTime<sensor_msgs::Imu, geometry_msgs::TransformStamped> MySyncPolicy;

  message_filters::Subscriber<sensor_msgs::Imu> imu_sub(nh,"raw_imu", 10);
  message_filters::Subscriber<geometry_msgs::TransformStamped> vicon_sub(nh,"vicon/Maya/Maya", 10);
  Synchronizer<MySyncPolicy> sync(MySyncPolicy(10), imu_sub, vicon_sub);
  sync.registerCallback(boost::bind(&odroid_node::imu_vicon_callback, &odnode, _1, _2));

  // TimeSynchronizer<sensor_msgs::Imu, geometry_msgs::PoseStamped> sync(imu_sub, vicon_sub, 10);
  // sync.registerCallback(boost::bind(&odroid_node::imu_vicon_callback, &odnode, _1, _2));
  // open communication through I2C
  // odnode.open_I2C();
  ros::Rate loop_rate(100); // rate for the node loop
  // int count = 0;
  while (ros::ok()){
    ros::spinOnce();
    if(odnode.getIMU()){
      odnode.ctl_callback();
      odnode.gazebo_controll();
    }
    loop_rate.sleep();
    // ++count;
  }
  return 0;
}

void odroid_node::callback(odroid::GainsConfig &config, uint32_t level) {
  ROS_INFO("Reconfigure Request: Update");
  print_f = config.print_f;
  print_imu = config.print_imu;
  print_thr = config.print_thr;
  print_test_variable = config.print_test_variable;
  // Attitude controller gains
  kR = config.kR;
  kW = config.kW;
  cR = config.cR;


  // if(MOTOR_ON && !MotorWarmup){
  kiR = config.kiR;
  kiX = config.kiX;
  // }
  // Position controller gains
  kx = config.kx;
  kv = config.kv;
  cX = config.cX;
  MOTOR_ON = config.Motor;
  MotorWarmup = config.MotorWarmup;
  xd(0) =  config.x;
  xd(1) =  config.y;
  xd(2) =  config.z;
  print_xd = config.print_xd;
  print_x_v = config.print_x_v;
  print_eX = config.print_eX;
  print_eV = config.print_eV;
  print_vicon = config.print_vicon;
  print_M = config.print_M;
  print_F = config.print_F;
  print_R_eb = config.print_R_eb;
}


void odroid_node::motor_command(){
  // Execute motor output commands
  for(int i = 0; i < 6; i++){
    //printf("Motor %i I2C write command of %i to address %i (%e N).\n", i, thr[i], mtr_addr[i], f[i]);
    tcflush(fhi2c, TCIOFLUSH);
    usleep(500);
    if(ioctl(fhi2c, I2C_SLAVE, mtr_addr[i])<0)
    printf("ERROR: ioctl\n");
    if(MOTOR_ON == false)// set motor speed to zero
    thr[i] = 0;
    else if(MotorWarmup == true)// warm up motors at 20 throttle command
    thr[i] = 20;
    while(write(fhi2c, &thr[i], 1)!=1)
    printf("ERROR: Motor %i I2C write command of %i to address %i (%e N) not sent.\n", i, thr[i], mtr_addr[i], f[i]);
  }

  std_msgs::String out;
  pub_.publish(out);
}

void odroid_node::open_I2C(){
  // Open i2c:
  fhi2c = open("/dev/i2c-1", O_RDWR);// Chris
  printf("Opening i2c port...\n");
  if(fhi2c!=3)
  printf("ERROR opening i2c port.\n");
  else
  printf("The i2c port is open.\n");
  usleep(100000);
  tcflush(fhi2c, TCIFLUSH);
  usleep(100000);

  // Call and response from motors
  printf("Checking motors...\n");
  int motornum, motoraddr, motorworks;
  int thr0 = 0;// 0 speed test command
  char PressEnter;

  for(motornum = 1; motornum <= 6; motornum++){
    motoraddr = motornum+40;// 1, 2, 3, ... -> 41, 42, 43, ...
    while(1){
      motorworks = 1;// will remain 1 if all works properly
      if(ioctl(fhi2c, I2C_SLAVE, motoraddr)<0){
        printf("ERROR: motor %i ioctl\n", motornum);
        motorworks = 0;// i2c address not called
      }
      usleep(10);
      if(write(fhi2c, &thr0, 1)!=1){
        printf("ERROR: motor %i write\n", motornum);
        motorworks = 0;
      }
      usleep(10);
      tcflush(fhi2c, TCIOFLUSH);
      if(motorworks == 1){
        printf("Motor %i working...\n", motornum);
        break;
      }
      else{
        printf("Fix motor %i, then press ENTER.\n\n", motornum);
        printf("Note: another i2c device may interupt the signal if\n");
        printf("any i2c wires are attached to unpowered components.\n");
        scanf("%c",&PressEnter);
        break;
      }
    }
  }
  printf("All motors are working.\n");
}

void odroid_node::GeometricController_6DOF(Vector3d xd, Vector3d xd_dot, Vector3d xd_ddot, Matrix3d Rd, Vector3d Wd, Vector3d Wddot, Vector3d x_e, Vector3d v_e, Vector3d W, Matrix3d R)
  {
    // Calculate eX (position error in inertial frame)
    Vector3d eX = x_e - xd;
    //cout<<"eX\n"<<eX.transpose()<<endl;
    if(print_eX){cout<<"eX: "<<eX.transpose()<<endl;}
    // Calculate eV (velocity error in inertial frame)
    Vector3d eV = v_e - xd_dot;
    //cout<<"eV\n"<<eV.transpose()<<endl;
    if(print_eV){cout<<"eV: "<<eV.transpose()<<endl;}
    // Calculate eR (rotation matrix error)
    // Take 9 elements of difference
    Matrix3d inside_vee_3by3 = Rd.transpose() * R - R.transpose() * Rd;
    //cout<<"inside_vee_3x3\n"<<inside_vee_3by3<<endl;
    Vector3d vee_3by1;
    eigen_invskew(inside_vee_3by3, vee_3by1);// 3x1
    //cout<<"vee_3by1\n"<<vee_3by1.transpose()<<endl;
    Vector3d eR = 0.5 * vee_3by1;
    //cout<<"eR\n"<<eR.transpose()<<endl;
	if(print_test_variable){
      printf("eR: %f | %f | %f \n", eR(0), eR(1), eR(2));
  }
  // Calculate eW (angular velocity error in body-fixed frame)
  Vector3d eW =  W -  R.transpose() * Rd * Wd;
  //cout<<"eW\n"<<eW.transpose()<<endl;
  // Update integral term of control
  // Position:
  eiX = eiX_last + del_t * eX;
  //cout<<"eiX\n"<<eiX<<endl;
  eiX_last = eiX;
  // Attitude:
  eiR = eiR_last + del_t * eR;// (c2*eR + eW);
  //cout<<"eiR\n"<<eiR<<endl;
  eiR_last = eiR;
  // Calculate 3 DOFs of F (controlled force in body-fixed frame)
  // MATLAB: F = R'*(-kx*ex-kv*ev-Ki*eiX-m*g*e3+m*xd_2dot);
  Vector3d A = - kx*eX - kv*eV - kiX*eiX + m*xd_ddot + Vector3d(0,0,-m*g);
  //cout<<"A\n"<<A.transpose()<<endl;
  F = R.transpose() * A;

  if(print_F){cout<<"F: "<<F.transpose()<<endl;}
  //cout<<"F\n"<<F.transpose()<<endl;
  // Calculate 3 DOFs of M (controlled moment in body-fixed frame)
  // MATLAB: M = -kR*eR-kW*eW-kRi*eiR+cross(W,J*W)+J*(R'*Rd*Wddot-hat(W)*R'*Rd*Wd);
  Matrix3d What;
  eigen_skew(W, What);
  //cout<<"What\n"<<What<<endl;
  M = -kR * eR - kW * eW-kiR * eiR + What * J * W + J * (R.transpose() * Rd * Wddot - What * R.transpose() * Rd * Wd);
  if(print_M){cout<<"M: "<<M.transpose()<<endl;}
  //cout<<"M\n"<<M.transpose()<<endl;
  // M[0] = -kR*eR[0]-kW*eW[0]-kiR_now*eiR[0]+What_J_W[0]+J_Jmult[0];
 // Convert forces & moments to f_i for i = 1:6 (forces of i-th prop)
  Matrix<double, 6, 1> FM;
  FM[0] = F[0];
  FM[1] = F[1];
  FM[2] = F[2];
  FM[3] = M[0];
  FM[4] = M[1];
  FM[5] = M[2];
    //cout<<"FM\n"<<FM.transpose()<<endl;

  double kxeX[3], kveV[3], kiXeiX[3], kReR[3], kWeW[3], kiReiR[3];
  for(int i = 0; i < 3; i++){
    kxeX[i] = kx*eX[i];
    kveV[i] = kv*eV[i];
    kiXeiX[i] = kiX*eiX[i];
    kReR[i] = kR*eR[i];
    kWeW[i] = kW*eW[i];
    kiReiR[i] = kiR*eiR[i];
  }
  f = invFMmat * FM;
  //cout<<"f\n"<<f.transpose()<<endl;

  }

void odroid_node::GeometricControl_SphericalJoint_3DOF_eigen(Vector3d Wd, Vector3d Wddot, Vector3d W, Matrix3d R, VectorXd eiR_last){
  Matrix3d Rd = MatrixXd::Identity(3,3);
  Vector3d e3(0,0,1), b3(0,0,1), vee_3by1;
  double l = 0;//.05;// length of rod connecting to the spherical joint
  Vector3d r = -l * b3;
  Vector3d F_g = m * g * R.transpose() * e3;
  Vector3d M_g = r.cross(F_g);
  //   Calculate eR (rotation matrix error)
  Matrix3d inside_vee_3by3 = Rd.transpose() * R - R.transpose() * Rd;
  eigen_invskew(inside_vee_3by3, vee_3by1);// 3x1
  Vector3d eR = 0.5 * vee_3by1;
  // Calculate eW (angular velocity error in body-fixed frame)
  Vector3d eW = W - R.transpose() * Rd * Wd;
  // Update integral term of control
  // Attitude:
  Vector3d eiR = eiR_last + del_t * eR;
  eiR_last = eiR;
  // Calculate 3 DOFs of M (controlled moment in body-fixed frame)
  // MATLAB: M = -kR*eR-kW*eW-kRi*eiR+cross(W,J*W)+J*(R'*Rd*Wddot-hat(W)*R'*Rd*Wd);
  Matrix3d What;
  eigen_skew(W, What);
  Vector3d What_J_W = What * J * W;
  Vector3d Jmult = R.transpose() * Rd * Wddot - What * R.transpose() * Rd * Wd;
  Vector3d J_Jmult = J * Jmult;
  Vector3d M = -kR * eR - kW * eW - kiR * eiR + What_J_W + J_Jmult - M_g;
  // To try different motor speeds, choose a force in the radial direction
  double F_req = -m*g;// N
  // Convert forces & moments to f_i for i = 1:6 (forces of i-th prop)
  VectorXd FM(6);
  FM << 0.0, 0.0, F_req, M(0), M(1), M(2);
  f = invFMmat * FM;
}
