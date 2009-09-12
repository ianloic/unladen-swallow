#ifndef UTIL_RUNTIMEFEEDBACK_FWD_H
#define UTIL_RUNTIMEFEEDBACK_FWD_H

#ifdef __cplusplus
extern "C" {
#endif

struct PyFeedbackMap;

struct PyFeedbackMap *PyFeedbackMap_New(void);
void PyFeedbackMap_Del(struct PyFeedbackMap *);
PyAPI_FUNC(void) PyFeedbackMap_Clear(struct PyFeedbackMap *);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* UTIL_RUNTIMEFEEDBACK_FWD_H */
