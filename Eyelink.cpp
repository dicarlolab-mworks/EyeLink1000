/*
 *  Eyelink.cpp
 *  Eyelink
 *
 *  Created by Philipp Schwedhelm on 13.12.10.
 *  Copyright 2010 German Primate Center. All rights reserved.
 *
 */

#include "Eyelink.h"


BEGIN_NAMESPACE_MW


// Allocate the lock on the heap so it's never destructed
Eyelink::unique_lock::mutex_type& Eyelink::eyelinkDriverLock = *(new unique_lock::mutex_type);

bool Eyelink::eyelinkInitialized = false;


const std::string Eyelink::IP("tracker_ip");
const std::string Eyelink::RX("eye_rx");
const std::string Eyelink::RY("eye_ry");
const std::string Eyelink::LX("eye_lx");
const std::string Eyelink::LY("eye_ly");
const std::string Eyelink::EX("eye_x");
const std::string Eyelink::EY("eye_y");
const std::string Eyelink::EZ("eye_z");
const std::string Eyelink::H_RX("href_rx");
const std::string Eyelink::H_RY("href_ry");
const std::string Eyelink::H_LX("href_lx");
const std::string Eyelink::H_LY("href_ly");
const std::string Eyelink::P_RX("pupil_rx");
const std::string Eyelink::P_RY("pupil_ry");
const std::string Eyelink::P_LX("pupil_lx");
const std::string Eyelink::P_LY("pupil_ly");
const std::string Eyelink::P_R("pupil_size_r");
const std::string Eyelink::P_L("pupil_size_l");
const std::string Eyelink::E_DIST("tracking_dist");
const std::string Eyelink::EYE_TIME("eye_time");
const std::string Eyelink::UPDATE_PERIOD("data_interval");


void Eyelink::describeComponent(ComponentInfo &info) {
    IODevice::describeComponent(info);
    
    info.setSignature("iodevice/eyelink");
    
    info.addParameter(IP);
    info.addParameter(RX, false);
    info.addParameter(RY, false);
    info.addParameter(LX, false);
    info.addParameter(LY, false);
    info.addParameter(EX, false);
    info.addParameter(EY, false);
    info.addParameter(EZ, false);
    info.addParameter(H_RX, false);
    info.addParameter(H_RY, false);
    info.addParameter(H_LX, false);
    info.addParameter(H_LY, false);
    info.addParameter(P_RX, false);
    info.addParameter(P_RY, false);
    info.addParameter(P_LX, false);
    info.addParameter(P_LY, false);
    info.addParameter(P_R, false);
    info.addParameter(P_L, false);
    info.addParameter(E_DIST, false);
    info.addParameter(EYE_TIME, false);
    info.addParameter(UPDATE_PERIOD);
}


static inline VariablePtr optionalVariable(const ParameterValue &param) {
    if (param.empty()) {
        return VariablePtr();
    }
    return VariablePtr(param);
}


Eyelink::Eyelink(const ParameterValueMap &parameters) :
    IODevice(parameters),
    tracker_ip(parameters[IP].str()),
    e_rx(optionalVariable(parameters[RX])),
    e_ry(optionalVariable(parameters[RY])),
    e_lx(optionalVariable(parameters[LX])),
    e_ly(optionalVariable(parameters[LY])),
    e_x(optionalVariable(parameters[EX])),
    e_y(optionalVariable(parameters[EY])),
    e_z(optionalVariable(parameters[EZ])),
    h_rx(optionalVariable(parameters[H_RX])),
    h_ry(optionalVariable(parameters[H_RY])),
    h_lx(optionalVariable(parameters[H_LX])),
    h_ly(optionalVariable(parameters[H_LY])),
    p_rx(optionalVariable(parameters[P_RX])),
    p_ry(optionalVariable(parameters[P_RY])),
    p_lx(optionalVariable(parameters[P_LX])),
    p_ly(optionalVariable(parameters[P_LY])),
    p_r(optionalVariable(parameters[P_R])),
    p_l(optionalVariable(parameters[P_L])),
    e_dist(parameters[E_DIST].empty() ? 0.0 : double(parameters[E_DIST])),
    e_time(optionalVariable(parameters[EYE_TIME])),
    update_period(parameters[UPDATE_PERIOD]),
    clock(Clock::instance()),
    errors(0),
    running(false)
{
    if ((e_dist == 0.0) && (e_x || e_y || e_z)) {
        throw SimpleException(M_IODEVICE_MESSAGE_DOMAIN, (boost::format("%s is required to compute %s, %s, and %s")
                                                          % E_DIST % EX % EY % EZ));
    }
}


Eyelink::~Eyelink() {
    unique_lock lock(eyelinkDriverLock);
    
    if (schedule_node) {
        schedule_node->cancel();
        schedule_node.reset();
    }
    
    if (eyelinkInitialized) {
        if (eyelink_is_connected()) {
            stop_recording();
            set_offline_mode();
            
            if (0 == close_data_file()) {
                mprintf(M_IODEVICE_MESSAGE_DOMAIN, "EyeLink closed data file \"%s\"", data_file_name);
            }
            
            // disconnect from tracker
            if (eyelink_close(1)) {
                merror(M_IODEVICE_MESSAGE_DOMAIN, "Cannot close EyeLink connection");
            } else {
                mprintf(M_IODEVICE_MESSAGE_DOMAIN,
                        "EyeLink %d system version %s disconnected",
                        tracker_version,
                        version_info);
            }
        }
        
        eyelinkInitialized = false;
    }
}


bool Eyelink::initialize() {
    unique_lock lock(eyelinkDriverLock);
    
    if (eyelinkInitialized) {
        merror(M_IODEVICE_MESSAGE_DOMAIN,
               "EyeLink was previously initialized.  Make sure your experiment contains only one EyeLink device.");
        return false;
    }
    
    if (set_eyelink_address(const_cast<char *>(tracker_ip.c_str()))) {
        merror(M_IODEVICE_MESSAGE_DOMAIN, "Cannot set EyeLink tracker's IP address to \"%s\"", tracker_ip.c_str());
        return false;
    }
    
    if (open_eyelink_connection(0)) {
        merror(M_IODEVICE_MESSAGE_DOMAIN, "Cannot connect to EyeLink tracker at %s", tracker_ip.c_str());
        return false;
    }
    
    // Open data file
    {
        // generate data file name
        time_t now = time(nullptr);
        struct tm* t = gmtime(&now);
        sprintf(data_file_name, "%02d%06d.edf",(t->tm_year-100),t->tm_yday*1440 + t->tm_hour*60 + t->tm_min);
        //YYMMMMMM : YY=Years since 2k, MMMMMM=Minutes in current year
        
        if (open_data_file(data_file_name)) {
            mwarning(M_IODEVICE_MESSAGE_DOMAIN, "Cannot open EyeLink data file \"%s\"", data_file_name);
        } else {
            mprintf(M_IODEVICE_MESSAGE_DOMAIN, "EyeLink logging to local file \"%s\"", data_file_name);
        }
    }
    
    // Tell the tracker what data to include in samples
    if (eyecmd_printf("link_sample_data = LEFT,RIGHT,GAZE,HREF,PUPIL,AREA")) {
        mwarning(M_IODEVICE_MESSAGE_DOMAIN,
                 "EyeLink did not respond to link_sample_data command; sample data may be incomplete");
    }
    
    // tell the tracker who we are
    eyelink_set_name(const_cast<char *>("MWorks_over_Socket"));
    
    // verify the name
    {
        ELINKNODE node;
        if (eyelink_get_node(0, &node)) {
            mwarning(M_IODEVICE_MESSAGE_DOMAIN, "Cannot retrieve EyeLink node name for this computer");
        } else {
            eyemsg_printf("%s connected", node.name);
        }
    }
    
    tracker_version = eyelink_get_tracker_version(version_info);
    if (0 == tracker_version) {
        merror(M_IODEVICE_MESSAGE_DOMAIN, "Cannot establish connection to EyeLink");
        return false;
    }
    
    mprintf(M_IODEVICE_MESSAGE_DOMAIN,
            "EyeLink %d system version %s connected via socket",
            tracker_version,
            version_info);
    
    eyelinkInitialized = true;
    
    return true;
}


bool Eyelink::startDeviceIO() {
    unique_lock lock(eyelinkDriverLock);
    
    if (!running) {
        if (!eyelink_is_connected()) {
            merror(M_IODEVICE_MESSAGE_DOMAIN, "Cannot start EyeLink: connection lost");
            return false;
        }
        
        // Set tracker to offline mode to stop whatever it may have been doing previously
        set_offline_mode();
        
        // Prepare link buffers to receive new data (and discard any old data)
        (void)eyelink_reset_data(1);  // Always returns zero
        
        // Start recording
        if (start_recording(0, 1, 1, 0)) {
            merror(M_IODEVICE_MESSAGE_DOMAIN, "Cannot start EyeLink recording");
            return false;
        }
        
        running = true;
        mprintf(M_IODEVICE_MESSAGE_DOMAIN, "EyeLink successfully started");
        
        boost::weak_ptr<Eyelink> weakThis(component_shared_from_this<Eyelink>());
        schedule_node = Scheduler::instance()->scheduleUS(std::string(FILELINE ": ") + getTag(),
                                                          update_period, //defer first start one period
                                                          update_period, //repeat interval
                                                          M_REPEAT_INDEFINITELY,
                                                          [weakThis]() {
                                                              if (auto sharedThis = weakThis.lock()) {
                                                                  sharedThis->update();
                                                              }
                                                              return nullptr;
                                                          },
                                                          M_DEFAULT_IODEVICE_PRIORITY,
                                                          M_DEFAULT_IODEVICE_WARN_SLOP_US,
                                                          M_DEFAULT_IODEVICE_FAIL_SLOP_US,
                                                          M_MISSED_EXECUTION_DROP);
    }
    
    return true;
}


static inline void assignMissingData(const boost::shared_ptr<Variable> &var, MWTime time) {
    if (var && var->getValue().getFloat() != MISSING_DATA) {
        var->setValue(float(MISSING_DATA), time);
    }
}


bool Eyelink::stopDeviceIO() {
    unique_lock lock(eyelinkDriverLock);
    
    if (running) {
        if (schedule_node) {
            schedule_node->cancel();
            schedule_node.reset();
        }
        
        if (!eyelink_is_connected()) {
            merror(M_IODEVICE_MESSAGE_DOMAIN, "Cannot stop EyeLink: connection lost");
            return false;
        }
        
        stop_recording();
        set_offline_mode();
        
        running = false;
        mprintf(M_IODEVICE_MESSAGE_DOMAIN, "EyeLink successfully stopped");
        
        // Set all outputs to MISSING_DATA
        auto currentTime = clock->getCurrentTimeUS();
        assignMissingData(e_rx, currentTime);
        assignMissingData(e_ry, currentTime);
        assignMissingData(e_lx, currentTime);
        assignMissingData(e_ly, currentTime);
        assignMissingData(e_x, currentTime);
        assignMissingData(e_y, currentTime);
        assignMissingData(e_z, currentTime);
        assignMissingData(h_rx, currentTime);
        assignMissingData(h_ry, currentTime);
        assignMissingData(h_lx, currentTime);
        assignMissingData(h_ly, currentTime);
        assignMissingData(p_rx, currentTime);
        assignMissingData(p_ry, currentTime);
        assignMissingData(p_lx, currentTime);
        assignMissingData(p_ly, currentTime);
        assignMissingData(p_r, currentTime);
        assignMissingData(p_l, currentTime);
        assignMissingData(e_time, currentTime);
    }
    
    return true;
}


static inline void assignValue(const boost::shared_ptr<Variable> &var, Datum value, MWTime time) {
    if (var) {
        var->setValue(value, time);
    }
}


void Eyelink::update() {
    unique_lock lock(eyelinkDriverLock);
    
    if (!running) {
        // If we've been stopped, don't try to read more data
        return;
    }
    
    if (eyelink_is_connected()) {
        while (eyelink_get_next_data(nullptr)) {
            if (eyelink_in_data_block(1, 0)) {  // only if data contains samples
                ALLF_DATA data;
                eyelink_get_float_data(&data);
                
                auto currentTime = clock->getCurrentTimeUS();
                assignValue(e_time, long(data.fs.time), currentTime);
                
                if (data.fs.type == SAMPLE_TYPE) {
                    handleSample(data.fs, currentTime);
                }
            }
        }
    } else {
        if (++errors * update_period > 1000000) { //just a quick hack but impossible to ignore by the user
            merror(M_IODEVICE_MESSAGE_DOMAIN, "EyeLink connection lost");
            errors = 0;
        }
    }
}


void Eyelink::handleSample(const FSAMPLE &sample, MWTime sampleTime) {
    if (sample.gx[RIGHT_EYE] != MISSING_DATA &&
        sample.gy[RIGHT_EYE] != MISSING_DATA &&
        sample.gx[LEFT_EYE] != MISSING_DATA &&
        sample.gy[LEFT_EYE] != MISSING_DATA &&
        (e_x || e_y || e_z))
    {
        double p43x = sample.gx[LEFT_EYE]/e_dist + 1;
        double p43y = sample.gy[LEFT_EYE]/e_dist;
        
        double p21x = sample.gx[RIGHT_EYE]/e_dist - 2;
        double p21y = sample.gy[RIGHT_EYE]/e_dist;
        
        double d4321 = p43x * p21x + p43y * p21y + 1;
        double d4343 = p43x * p43x + p43y * p43y + 1;
        double d2121 = p21x * p21x + p21y * p21y + 1;
        
        double denom = d2121 * d4343 - d4321 * d4321;
        
        if (std::abs(denom) > 1e-6) { // should always be true when e_dist is really tracking range
            double numer = p43x * d4321 - p21x * d4343;
            
            double mua = numer / denom;
            double mub = (p43x + d4321 * mua) / d4343;
            
            double pax = 1 + mua * p21x;
            double pay = mua * p21y;
            double paz = -1 + mua;
            double pbx = mub * p43x;
            double pby = mub * p43y;
            double pbz = -1 + mub;
            
            assignValue(e_x, pax + 0.5*(pbx-pax), sampleTime);
            assignValue(e_y, pay + 0.5*(pby-pay), sampleTime);
            assignValue(e_z, paz + 0.5*(pbz-paz), sampleTime);
        }
    } else {
        assignMissingData(e_x, sampleTime);
        assignMissingData(e_y, sampleTime);
        assignMissingData(e_z, sampleTime);
    }
    
    if (sample.gx[RIGHT_EYE] != MISSING_DATA &&
        sample.gy[RIGHT_EYE] != MISSING_DATA)
    {
        assignValue(e_rx, sample.gx[RIGHT_EYE], sampleTime);
        assignValue(e_ry, sample.gy[RIGHT_EYE], sampleTime);
    } else {
        assignMissingData(e_rx, sampleTime);
        assignMissingData(e_ry, sampleTime);
    }
    
    if (sample.gx[LEFT_EYE] != MISSING_DATA &&
        sample.gy[LEFT_EYE] != MISSING_DATA)
    {
        assignValue(e_lx, sample.gx[LEFT_EYE], sampleTime);
        assignValue(e_ly, sample.gy[LEFT_EYE], sampleTime);
    } else {
        assignMissingData(e_lx, sampleTime);
        assignMissingData(e_ly, sampleTime);
    }
    
    if (sample.hx[RIGHT_EYE] != -7936.0f &&
        sample.hy[RIGHT_EYE] != -7936.0f)
    {
        assignValue(h_rx, sample.hx[RIGHT_EYE], sampleTime);
        assignValue(h_ry, sample.hy[RIGHT_EYE], sampleTime);
    } else {
        assignMissingData(h_rx, sampleTime);
        assignMissingData(h_ry, sampleTime);
    }
    
    if (sample.hx[LEFT_EYE] != -7936.0f &&
        sample.hy[LEFT_EYE] != -7936.0f)
    {
        assignValue(h_lx, sample.hx[LEFT_EYE], sampleTime);
        assignValue(h_ly, sample.hy[LEFT_EYE], sampleTime);
    } else {
        assignMissingData(h_lx, sampleTime);
        assignMissingData(h_ly, sampleTime);
    }
    
    if (sample.px[RIGHT_EYE] != MISSING_DATA &&
        sample.py[RIGHT_EYE] != MISSING_DATA)
    {
        assignValue(p_rx, sample.px[RIGHT_EYE], sampleTime);
        assignValue(p_ry, sample.py[RIGHT_EYE], sampleTime);
    } else {
        assignMissingData(p_rx, sampleTime);
        assignMissingData(p_ry, sampleTime);
    }
    
    if (sample.px[LEFT_EYE] != MISSING_DATA &&
        sample.py[LEFT_EYE] != MISSING_DATA)
    {
        assignValue(p_lx, sample.px[LEFT_EYE], sampleTime);
        assignValue(p_ly, sample.py[LEFT_EYE], sampleTime);
    } else {
        assignMissingData(p_lx, sampleTime);
        assignMissingData(p_ly, sampleTime);
    }
    
    if (sample.pa[RIGHT_EYE] != 0) {
        assignValue(p_r, sample.pa[RIGHT_EYE], sampleTime);
    } else {
        assignMissingData(p_r, sampleTime);
    }
    
    if (sample.pa[LEFT_EYE] != 0) {
        assignValue(p_l, sample.pa[LEFT_EYE], sampleTime);
    } else {
        assignMissingData(p_l, sampleTime);
    }
}


END_NAMESPACE_MW
