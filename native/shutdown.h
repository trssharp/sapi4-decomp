#ifndef S4_SHUTDOWN_INTERNAL_H
#define S4_SHUTDOWN_INTERNAL_H
int s4_shared_state_shutdown(void);
int vc_resources_shutdown(void);
void vc_token_reset(void);
void s4_shared_shutdown(void);
void s4_image_shutdown(void);
#endif
