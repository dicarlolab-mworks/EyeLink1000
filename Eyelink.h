/*
 *  Eyelink.h
 *  Eyelink
 *
 *  Created by Philipp Schwedhelm on 13.12.10.
 *  Copyright 2010 German Primate Center. All rights reserved.
 *
 */

#ifndef Eyelink_H_
#define Eyelink_H_


BEGIN_NAMESPACE_MW


class Eyelink : public IODevice, boost::noncopyable {
    
public:
    static const std::string IP;
    static const std::string RX;
    static const std::string RY;
    static const std::string LX;
    static const std::string LY;
    static const std::string EX;
    static const std::string EY;
    static const std::string EZ;
    static const std::string H_RX;
    static const std::string H_RY;
    static const std::string H_LX;
    static const std::string H_LY;
    static const std::string P_RX;
    static const std::string P_RY;
    static const std::string P_LX;
    static const std::string P_LY;
    static const std::string P_R;
    static const std::string P_L;
    static const std::string E_DIST;
    static const std::string EYE_TIME;
    static const std::string UPDATE_PERIOD;
    
    static void describeComponent(ComponentInfo &info);
    
    explicit Eyelink(const ParameterValueMap &parameters);
    ~Eyelink();
    
    bool initialize() override;
    bool startDeviceIO() override;
    bool stopDeviceIO() override;
    
private:
    bool update();
    
    using unique_lock = std::unique_lock<std::mutex>;
    static unique_lock::mutex_type& eyelinkDriverLock;
    static bool eyelinkInitialized;
    
    boost::shared_ptr<ScheduleTask> schedule_node;
    boost::shared_ptr<Clock> clock;
    
    const std::string tracker_ip;
    boost::shared_ptr<Variable> e_rx;
    boost::shared_ptr<Variable> e_ry;
    boost::shared_ptr<Variable> e_lx;
    boost::shared_ptr<Variable> e_ly;
    boost::shared_ptr<Variable> e_x;
    boost::shared_ptr<Variable> e_y;
    boost::shared_ptr<Variable> e_z;
    boost::shared_ptr<Variable> h_rx;
    boost::shared_ptr<Variable> h_ry;
    boost::shared_ptr<Variable> h_lx;
    boost::shared_ptr<Variable> h_ly;
    boost::shared_ptr<Variable> p_rx;
    boost::shared_ptr<Variable> p_ry;
    boost::shared_ptr<Variable> p_lx;
    boost::shared_ptr<Variable> p_ly;
    boost::shared_ptr<Variable> p_r;
    boost::shared_ptr<Variable> p_l;
    double e_dist;
    boost::shared_ptr<Variable> e_time;
    
    const MWorksTime update_period;
    
    char version_info[256];
    int tracker_version;
    char data_file_name[11];
    bool stopped;
    int errors;
    
};


END_NAMESPACE_MW


#endif /* Eyelink_H_ */
