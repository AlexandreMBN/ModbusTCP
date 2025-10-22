#include "PID.h"   
#include "stdint.h"   
   
/*! \brief Initialisation of PID controller parameters.  
 *  
 *  Initialise the variables used by the PID algorithm.  
 *  
 *  \param p_factor  Proportional term.  
 *  \param i_factor  Integral term.  
 *  \param d_factor  Derivate term.  
 *  \param pid  Struct with PID status.  
 */   
// void pid_Init(int16_t p_factor, int16_t i_factor, int16_t d_factor, pidData_t *pid)   
// // Set up PID controller parameters   
// {   
//   // Start values for PID controller   
//   pid->sumError = 0;   
//   pid->lastProcessValue = 0;   
//   // Tuning constants for PID loop   
//   pid->P_Factor = p_factor;   
//   pid->I_Factor = i_factor;   
//   pid->D_Factor = d_factor;   
//   // Limits to avoid overflow
//   pid->maxError = MAX_INT / (pid->P_Factor + 1);   
//   pid->maxSumError = MAX_I_TERM / (pid->I_Factor + 1);   
// }   
   
   
/*! \brief PID control algorithm.  
 *  
 *  Calculates output from setpoint, process value and PID status.  
 *  
 *  \param setPoint  Desired value.  
 *  \param processValue  Measured value.  
 *  \param pid_st  PID status struct.  
 */  
// int16_t pid_Controller(int16_t setPoint, int16_t processValue, pidData_t *pid_st)   
// { //static int8_t divi=0;  
//   int32_t error, p_term, d_term;   
//   int32_t i_term, ret, temp;   
//    
//   error = setPoint - processValue;   
//    
//   // Calculate Pterm and limit error overflow   
//   if (error > pid_st->maxError){   
//     p_term = MAX_INT;
//   }
//   else if (error < -pid_st->maxError){
//     p_term = -MAX_INT;
//   }
//   else{
//     p_term = pid_st->P_Factor * error;   
//   }   
//    
//   // Calculate Iterm and limit integral runaway
//   temp = pid_st->sumError + error;
//   if(temp > pid_st->maxSumError){   
//     i_term = MAX_I_TERM;
//     pid_st->sumError = pid_st->maxSumError;   
//   }   
//   else if(temp < -pid_st->maxSumError){   
//     i_term = -MAX_I_TERM;
//     pid_st->sumError = -pid_st->maxSumError;   
//   }
//   else{
//     pid_st->sumError = temp;
//     i_term = pid_st->I_Factor * pid_st->sumError;
//   }
// 
//   // Calculate Dterm
//   d_term = pid_st->D_Factor * (pid_st->lastProcessValue - processValue);   
//   
//   pid_st->lastProcessValue = processValue;
//   
//   ret = (p_term + (i_term)/10+ d_term) / SCALING_FACTOR;// 
//   if(ret > MAX_INT){
//     ret = MAX_INT;
//   }
//   else if(ret < -MAX_INT){   
//     ret = -MAX_INT;   
//   }
//   
//   return((int16_t)ret);   
// }   
   
/*! \brief Resets the integrator.  
 *  
 *  Calling this function will reset the integrator in the PID regulator.  
 */   
// void pid_Reset_Integrator(pidData_t *pid_st)   
// {   
//   pid_st->sumError = 0;   
// }   


void pid_zeroize(PID* pid) {
	// set prev and integrated error to zero
	//pid->prev_error = 0;
	pid->int_error = 0;
}

void pid_set(PID* pid, double Pg, double Ig, double Dg, double Wg)
{
	pid->proportional_gain	= Pg;
	pid->integral_gain		= Ig;
	pid->derivative_gain	= Dg;
	pid->windup_guard		= Wg;
}

double pid_update(PID* pid, double curr_error, double dt) {
	double diff;
	double p_term;
	double i_term;
	double d_term;
	
	// integration with windup guarding
	pid->int_error += (curr_error * dt);
	if (pid->int_error < -(pid->windup_guard))
	{
	pid->int_error = -(pid->windup_guard);
	}
	else if (pid->int_error > pid->windup_guard)
	{
	pid->int_error = pid->windup_guard;
	}
	// differentiation
	diff = ((curr_error - pid->prev_error) / dt);

	// scaling
	p_term = (pid->proportional_gain * curr_error);
	i_term = (pid->integral_gain     * pid->int_error);
	d_term = (pid->derivative_gain   * diff);

	// summation of terms
	pid->control = p_term + i_term + d_term;

	// save current error as previous error for next iteration
	pid->prev_error = curr_error;
	
	return pid->control;
}