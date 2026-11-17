#ifndef BB_FAULT_NOTICE_H
#define BB_FAULT_NOTICE_H
                                                                             
  
                                                                            
                                                                        
                                                          
  
                                                                          
                                                                   
                                                                         
                                                                         
                                                                         
                                                                            
                                                                            
                                                          
                                                                          
                                                                           
   
enum {
    BB_NOTICE_NONE=0,
    BB_NOTICE_CANCELLED_BY_STOP=1,
    BB_NOTICE_RENDER_FAULT_STOP=2,
    BB_NOTICE_WORKER_FAULT_STOP=3,
    BB_NOTICE_REQUEST_FAULT=4,
    BB_NOTICE_COUNT
};
static inline const char *bb_notice_text(unsigned id) {
    return id==BB_NOTICE_CANCELLED_BY_STOP?"CANCELLED BY STOP":
           id==BB_NOTICE_RENDER_FAULT_STOP?"AUDIO STOPPED: RENDER FAULT":
           id==BB_NOTICE_WORKER_FAULT_STOP?"AUDIO FAULT: SAVE, RESTART":
           id==BB_NOTICE_REQUEST_FAULT?"AUDIO FAULT: REQUEST ENDED":"";
}
#endif
