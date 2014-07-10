#ifndef H_IPA_SOCKETCAN_DRIVER
#define H_IPA_SOCKETCAN_DRIVER

#include <ipa_can_interface/asio_base.h>
#include <boost/bind.hpp>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
 
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>

namespace ipa_can {

template<typename FrameDelegate, typename StateDelegate> class SocketCANDriver : public AsioDriver<FrameDelegate,StateDelegate,boost::asio::posix::stream_descriptor> {
    typedef AsioDriver<FrameDelegate,StateDelegate,boost::asio::posix::stream_descriptor> BaseClass;
public:    
    SocketCANDriver(FrameDelegate frame_delegate, StateDelegate state_delegate)
    : BaseClass(frame_delegate, state_delegate)
    {}
    bool init(const std::string &device, unsigned int bitrate){
        State s = BaseClass::getState();
        if(s.driver_state == State::closed){
            device_ = device;
            if(bitrate != 0) return false; // not supported, TODO: use libsocketcan

            int sc = socket( PF_CAN, SOCK_RAW, CAN_RAW );
            std::cout << "sc " << sc << std::endl;
            if(sc < 0){
                BaseClass::setErrorCode(boost::system::error_code(sc,boost::system::system_category()));
                return false;
            }
            
            struct ifreq ifr;
            strcpy(ifr.ifr_name, device_.c_str());
            int ret = ioctl(sc, SIOCGIFINDEX, &ifr);

            std::cout << "ret1 " << ret << std::endl;
            if(ret != 0){
                BaseClass::setErrorCode(boost::system::error_code(ret,boost::system::system_category()));
                close(sc);
                return false;
            }

            can_err_mask_t err_mask = ( CAN_ERR_TX_TIMEOUT | CAN_ERR_BUSOFF ); //TODO select errors to track

            ret = setsockopt(sc, SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
               &err_mask, sizeof(err_mask));
            
            if(ret != 0){
                BaseClass::setErrorCode(boost::system::error_code(ret,boost::system::system_category()));
                close(sc);
                return false;
            }
            
            struct sockaddr_can addr;
            addr.can_family = AF_CAN;
            addr.can_ifindex = ifr.ifr_ifindex;
            ret = bind( sc, (struct sockaddr*)&addr, sizeof(addr) );            

            std::cout << "ret2 " << ret << std::endl;
            if(ret != 0){
                BaseClass::setErrorCode(boost::system::error_code(ret,boost::system::system_category()));
                close(sc);
                return false;
            }
            
            boost::system::error_code ec;
            BaseClass::socket_.assign(sc,ec);
            
            BaseClass::setErrorCode(ec);
            
            if(ec){
                std::cout << "!ASSIGN"<< std::endl;
                close(sc);
                return false;
            }
            BaseClass::setDriverState(State::open);
            return true;
        }else  std::cout << "NO CLOSED" << std::endl;
        return false;
    }
    bool recover(){
        State s = BaseClass::getState();
        if(s.driver_state == State::open){
            BaseClass::shutdown();
            return init(device_, 0);
        }
    }
    bool translateError(unsigned int internal_error, std::string & str){
        return false; // TODO
    }
protected:
    std::string device_;
    can_frame frame_;
    
    void triggerReadSome(){
        BaseClass::socket_.async_read_some(boost::asio::buffer(&frame_, sizeof(frame_)), boost::bind( &SocketCANDriver::readFrame,this, boost::asio::placeholders::error));
    }
    
    bool enqueue(const Frame & msg){
        boost::mutex::scoped_lock lock(send_mutex_); //TODO: timed try lock

        can_frame frame;
        frame.can_id = msg.id | (msg.is_extended?CAN_EFF_FLAG:0) | (msg.is_rtr?CAN_RTR_FLAG:0);;
        frame.can_dlc = msg.dlc;
        
        
        for(int i=0; i < frame.can_dlc;++i)
            frame.data[i] = msg.data[i];
        
        boost::system::error_code ec;
        boost::asio::write(BaseClass::socket_, boost::asio::buffer(&frame, sizeof(frame)),boost::asio::transfer_all(), ec);
        if(ec){
            BaseClass::setErrorCode(ec);
            BaseClass::setDriverState(State::open);
            return false;
        }
        
        return true;
    }
    
    void readFrame(const boost::system::error_code& error){
        if(!error){
            if(frame_.can_id & CAN_ERR_FLAG){ // error message
                // TODO
                std::cout << "error" << std::endl;
            }

            BaseClass::input_.is_extended = frame_.can_id & CAN_EFF_FLAG;
            BaseClass::input_.id = frame_.can_id & (BaseClass::input_.is_extended ? CAN_EFF_MASK : CAN_SFF_MASK);
            BaseClass::input_.is_error = frame_.can_id & CAN_ERR_FLAG;
            BaseClass::input_.is_rtr = frame_.can_id & CAN_RTR_FLAG;
            BaseClass::input_.dlc = frame_.can_dlc;
            for(int i=0;i<frame_.can_dlc && i < 8; ++i){
                BaseClass::input_.data[i] = frame_.data[i];
            }
        }
        BaseClass::frameReceived(error);
    }
private:
    boost::mutex send_mutex_;
};
    
}; // namespace ipa_can
#endif