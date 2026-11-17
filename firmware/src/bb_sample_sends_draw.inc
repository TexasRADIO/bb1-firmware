/* SPDX-License-Identifier: GPL-3.0-only */
#if BB_SAMPLE_HOST_INTEGRATION && BB_WITH_EFFECTS
static void bb_sample_sends_draw(const bb_sample_status *view,int route){
    static const char *const labels[4]={"ECHO %","SPRING %","LEVEL %","GAIN %"};
    cv_text(9,5,&AF_M,"SAMPLE / SENDS",BB_TEXT);
    int ready=route>0&&view->queue_idle&&!view->stop_pending;
    for(unsigned k=0;k<4;k++){
        int x=9+(int)k*58;
        cv_text(x,36,&AF_S,labels[k],BB_DIM);
        if(ready)bb_number(x,53,bb_sample_sends_display(view,k),&AF_M,BB_TEXT);
        else cv_text(x,57,&AF_S,"WAIT",BB_DIM);
    }
    cv_text(9,82,&AF_S,"WHOLE LAYER / GAIN 1-100%",BB_DIM);
    cv_text(9,105,&AF_S,"EDIT: NEXT BANK / HOME: STUDIO",BB_DIM);
    if(!ready)bb_dirty=1; /* Repaint owner values after the actual FIFO drains. */
}
#endif
