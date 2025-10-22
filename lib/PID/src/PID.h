#ifndef PID_H 
#define PID_H 
 
#include "stdint.h" 


typedef struct {
	double windup_guard;
	double proportional_gain;
	double integral_gain;
	double derivative_gain;
	double prev_error;
	double int_error;
	double control;
} PID;

 
#define SCALING_FACTOR  128
 
/*! \brief PID Status 
 * 
 * Setpoints and data used by the PID control algorithm 
 */ 
// typedef struct PID_DATA{ 
//   //! Last process value, used to find derivative of process value. 
//   int16_t lastProcessValue; 
//   //! Summation of errors, used for integrate calculations 
//   int32_t sumError; 
//   //! The Proportional tuning constant, multiplied with SCALING_FACTOR 
//   int16_t P_Factor; 
//   //! The Integral tuning constant, multiplied with SCALING_FACTOR 
//   int16_t I_Factor; 
//   //! The Derivative tuning constant, multiplied with SCALING_FACTOR 
//   int16_t D_Factor; 
//   //! Maximum allowed error, avoid overflow 
//   int16_t maxError; 
//   //! Maximum allowed sumerror, avoid overflow 
//   int32_t maxSumError; 
// } pidData_t; 
 
/*! \brief Maximum values 
 * 
 * Needed to avoid sign/overflow problems 
 */ 
// Maximum value of variables 
#define MAX_INT         INT16_MAX 
#define MAX_LONG        INT32_MAX 
#define MAX_I_TERM      (MAX_LONG / 2) 
 
// Boolean values 
#define FALSE           0
#define TRUE            1
 
// void pid_Init(int16_t p_factor, int16_t i_factor, int16_t d_factor, pidData_t *pid); 
// int16_t pid_Controller(int16_t setPoint, int16_t processValue, pidData_t *pid_st); 
// void pid_Reset_Integrator(pidData_t *pid_st);
//Outro PID
void pid_zeroize(PID* pid);
void pid_set(PID* pid, double Pg, double Ig, double Dg, double Wg);
double pid_update(PID* pid, double curr_error, double dt); 
 
#endif 