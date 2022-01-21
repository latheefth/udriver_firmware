/*
 * main_helper.c
 *
 *  Created on: Nov 3, 2016
 *      Author: fwidmaierlocal
 */
#include "main_helper.h"
#include <math.h>


void pidSetup(PID_Handle pidHandle[], USER_Params userParams)
{
	// This equation uses the scaled maximum voltage vector, which is
	// already in per units, hence there is no need to include the #define
	// for USER_IQ_FULL_SCALE_VOLTAGE_V
	_iq maxVoltage_pu = _IQ(userParams.maxVsMag_pu *
			userParams.voltage_sf);

	float_t fullScaleCurrent = userParams.iqFullScaleCurrent_A;
	float_t fullScaleVoltage = userParams.iqFullScaleVoltage_V;
	float_t IsrPeriod_sec = 1.0e-6 * userParams.pwmPeriod_usec *
			userParams.numPwmTicksPerIsrTick;
	float_t Ls_d = userParams.motor_Ls_d;
	float_t Ls_q = userParams.motor_Ls_q;
	float_t Rs = userParams.motor_Rs;

	// This lab assumes that motor parameters are known, and it does not
	// perform motor ID, so the R/L parameters are known and defined in
	// user.h
	float_t RoverLs_d = Rs / Ls_d;
	float_t RoverLs_q = Rs / Ls_q;

	// For the current controller, Kp = Ls*bandwidth(rad/sec)  But in order
	// to be used, it must be converted to per unit values by multiplying
	// by fullScaleCurrent and then dividing by fullScaleVoltage.  From the
	// statement below, we see that the bandwidth in rad/sec is equal to
	// 0.25/IsrPeriod_sec, which is equal to USER_ISR_FREQ_HZ/4. This means
	// that by setting Kp as described below, the bandwidth in Hz is
	// USER_ISR_FREQ_HZ/(8*pi).
	_iq Kp_Id = _IQ((0.25 * Ls_d * fullScaleCurrent) / (IsrPeriod_sec
			* fullScaleVoltage));

	// In order to achieve pole/zero cancellation (which reduces the
	// closed-loop transfer function from a second-order system to a
	// first-order system), Ki must equal Rs/Ls.  Since the output of the
	// Ki gain stage is integrated by a DIGITAL integrator, the integrator
	// input must be scaled by 1/IsrPeriod_sec.  That's just the way
	// digital integrators work.  But, since IsrPeriod_sec is a constant,
	// we can save an additional multiplication operation by lumping this
	// term with the Ki value.
	_iq Ki_Id = _IQ(RoverLs_d * IsrPeriod_sec);

	// Now do the same thing for Kp for the q-axis current controller.
	// If the motor is not an IPM motor, Ld and Lq are the same, which
	// means that Kp_Iq = Kp_Id
	_iq Kp_Iq = _IQ((0.25 * Ls_q * fullScaleCurrent) / (IsrPeriod_sec
			* fullScaleVoltage));

	// Do the same thing for Ki for the q-axis current controller.  If the
	// motor is not an IPM motor, Ld and Lq are the same, which means that
	// Ki_Iq = Ki_Id.
	_iq Ki_Iq = _IQ(RoverLs_q * IsrPeriod_sec);


	// The following instructions load the parameters for the d-axis
	// current controller.
	// P term = Kp_Id, I term = Ki_Id, D term = 0
	PID_setGains(pidHandle[1], Kp_Id, Ki_Id, _IQ(0.0));

	// Largest negative voltage = -maxVoltage_pu, largest positive
	// voltage = maxVoltage_pu
	PID_setMinMax(pidHandle[1], -maxVoltage_pu, maxVoltage_pu);

	// Set the initial condition value for the integrator output to 0
	PID_setUi(pidHandle[1], _IQ(0.0));

	// The following instructions load the parameters for the q-axis
	// current controller.
	// P term = Kp_Iq, I term = Ki_Iq, D term = 0
	PID_setGains(pidHandle[2], Kp_Iq, Ki_Iq, _IQ(0.0));

	// The largest negative voltage = 0 and the largest positive
	// voltage = 0.  But these limits are updated every single ISR before
	// actually executing the Iq controller. The limits depend on how much
	// voltage is left over after the Id controller executes. So having an
	// initial value of 0 does not affect Iq current controller execution.
	PID_setMinMax(pidHandle[2], _IQ(0.0), _IQ(0.0));

	// Set the initial condition value for the integrator output to 0
	PID_setUi(pidHandle[2], _IQ(0.0));
}


void setupClarke_I(CLARKE_Handle handle, const uint_least8_t numCurrentSensors)
{
	_iq alpha_sf, beta_sf;

	// initialize the Clarke transform module for current
	if(numCurrentSensors == 3)
	{
		alpha_sf = _IQ(MATH_ONE_OVER_THREE);
		beta_sf = _IQ(MATH_ONE_OVER_SQRT_THREE);
	}
	else if(numCurrentSensors == 2)
	{
		alpha_sf = _IQ(1.0);
		beta_sf = _IQ(MATH_ONE_OVER_SQRT_THREE);
	}
	else
	{
		alpha_sf = _IQ(0.0);
		beta_sf = _IQ(0.0);
	}

	// set the parameters
	CLARKE_setScaleFactors(handle, alpha_sf, beta_sf);
	CLARKE_setNumSensors(handle, numCurrentSensors);

	return;
} // end of setupClarke_I() function


void setupClarke_V(CLARKE_Handle handle, const uint_least8_t numVoltageSensors)
{
	_iq alpha_sf, beta_sf;

	// initialize the Clarke transform module for voltage
	if(numVoltageSensors == 3)
	{
		alpha_sf = _IQ(MATH_ONE_OVER_THREE);
		beta_sf = _IQ(MATH_ONE_OVER_SQRT_THREE);
	}
	else
	{
		alpha_sf = _IQ(0.0);
		beta_sf = _IQ(0.0);
	}

	// In other words, the only acceptable number of voltage sensors is three.
	// set the parameters
	CLARKE_setScaleFactors(handle, alpha_sf, beta_sf);
	CLARKE_setNumSensors(handle, numVoltageSensors);

	return;
} // end of setupClarke_V() function


//! \brief  Call this function to fix 1p6. This is only used for F2806xF/M
//! \brief  implementation of InstaSPIN (version 1.6 of ROM) since the
//! \brief  inductance calculation is not done correctly in ROM, so this
//! \brief  function fixes that ROM bug.
void softwareUpdate1p6(EST_Handle handle, USER_Params *pUserParams)
{
	float_t iqFullScaleVoltage_V = pUserParams->iqFullScaleVoltage_V;
	float_t iqFullScaleCurrent_A = pUserParams->iqFullScaleCurrent_A;
	float_t voltageFilterPole_rps = pUserParams->voltageFilterPole_rps;
	float_t motorLs_d = pUserParams->motor_Ls_d;
	float_t motorLs_q = pUserParams->motor_Ls_q;

	float_t fullScaleInductance = iqFullScaleVoltage_V
			/ (iqFullScaleCurrent_A
					* voltageFilterPole_rps);
	float_t Ls_coarse_max = _IQ30toF(EST_getLs_coarse_max_pu(handle));
	int_least8_t lShift = ceil(log(motorLs_d / (Ls_coarse_max
			* fullScaleInductance)) / log(2.0));
	uint_least8_t Ls_qFmt = 30 - lShift;
	float_t L_max = fullScaleInductance * pow(2.0, lShift);
	_iq Ls_d_pu = _IQ30(motorLs_d / L_max);
	_iq Ls_q_pu = _IQ30(motorLs_q / L_max);

	// store the results
	EST_setLs_d_pu(handle, Ls_d_pu);
	EST_setLs_q_pu(handle, Ls_q_pu);
	EST_setLs_qFmt(handle, Ls_qFmt);

	return;
} // end of softwareUpdate1p6() function



void PIE_registerTimer0IntHandler(PIE_Handle pieHandle, PIE_IntVec_t isr)
{
	PIE_Obj *pie = (PIE_Obj *)pieHandle;
	ENABLE_PROTECTED_REGISTER_WRITE_MODE;
	pie->TINT0 = isr;
	DISABLE_PROTECTED_REGISTER_WRITE_MODE;
}


void setupXINT1(HAL_Handle halHandle, PIE_IntVec_t isr)
{
    HAL_Obj *hal = (HAL_Obj*)halHandle;
    PIE_Obj *pie = (PIE_Obj *)hal->pieHandle;
    ENABLE_PROTECTED_REGISTER_WRITE_MODE;
    pie->XINT1 = isr;
    pie->XINTnCR[0] |= 4; // interrupt on RISING EDGE
    pie->XINTnCR[0] |= 8; // interrupt on FALLING EDGE
    pie->XINTnCR[0] |= 1; // enable XINT1 interrupt
    DISABLE_PROTECTED_REGISTER_WRITE_MODE;

    PIE_enableInt(hal->pieHandle, PIE_GroupNumber_1, PIE_InterruptSource_XINT_1);
    CPU_enableInt(hal->cpuHandle, CPU_IntNumber_1);
}

void setupSPIBRXInt(HAL_Handle halHandle, PIE_IntVec_t isr)
{
    HAL_Obj *hal = (HAL_Obj*)halHandle;
    PIE_Obj *pie = (PIE_Obj *)hal->pieHandle;
    ENABLE_PROTECTED_REGISTER_WRITE_MODE;
    pie->SPIRXINTB = isr;
    DISABLE_PROTECTED_REGISTER_WRITE_MODE;

    PIE_enableInt(hal->pieHandle, PIE_GroupNumber_6, PIE_InterruptSource_SPIBRX);
    CPU_enableInt(hal->cpuHandle, CPU_IntNumber_6);

}


extern void HAL_overwriteSetupGpio(HAL_Handle halHandle)
{
	HAL_Obj *hal = (HAL_Obj*)halHandle;

	//*** config GPIO pins for the external LEDs
	GPIO_setLow(hal->gpioHandle, LED_EXTERN_RED);
	GPIO_setDirection(hal->gpioHandle,
			LED_EXTERN_RED,
			GPIO_Direction_Output);

	GPIO_setLow(hal->gpioHandle, LED_EXTERN_GREEN);
	GPIO_setDirection(hal->gpioHandle,
			LED_EXTERN_GREEN,
			GPIO_Direction_Output);

	GPIO_setLow(hal->gpioHandle, LED_EXTERN_YELLOW);
	GPIO_setDirection(hal->gpioHandle,
			LED_EXTERN_YELLOW,
			GPIO_Direction_Output);
}


void overwriteSetupTimer0(HAL_Handle handle, const uint32_t timerPeriod_counts)
{
  HAL_Obj  *obj = (HAL_Obj *)handle;

  // use timer 0 for SPI transmissions
  TIMER_setDecimationFactor(obj->timerHandle[0], 0);
  TIMER_setEmulationMode(obj->timerHandle[0], TIMER_EmulationMode_RunFree);
  TIMER_setPeriod(obj->timerHandle[0], timerPeriod_counts);
  TIMER_setPreScaler(obj->timerHandle[0], 0);

  return;
}  // end of HAL_setupTimers() function


void setupQepIndexInterrupt(HAL_Handle halHandle, HAL_Handle_mtr halHandleMtr[2],
		PIE_IntVec_t qep1IndexIsr, PIE_IntVec_t qep2IndexIsr)
{
	uint_least8_t mtrNum;
	HAL_Obj *halObj = (HAL_Obj *)halHandle;
	HAL_Obj_mtr *halMtrObj;
	PIE_Obj *pie = (PIE_Obj *)halObj->pieHandle;

	// specify ISRs
	ENABLE_PROTECTED_REGISTER_WRITE_MODE;
	pie->EQEP1_INT = qep1IndexIsr;
	pie->EQEP2_INT = qep2IndexIsr;
	//PIE_registerPieIntHandler(obj->pieHandle, PIE_GroupNumber_5, PIE_InterruptSource_EQEP1, &qepIndexISR);
	DISABLE_PROTECTED_REGISTER_WRITE_MODE;

	for(mtrNum=HAL_MTR1; mtrNum <= HAL_MTR2; mtrNum++)
	{
		halMtrObj = (HAL_Obj_mtr *)halHandleMtr[mtrNum];

		// enable QEP interrupt for index
		QEP_clear_all_interrupt_flags(halMtrObj->qepHandle);
		QEP_enable_interrupt(halMtrObj->qepHandle, QEINT_Iel);
	}

	// enable the corresponding interrupts in PIE (group 5)
	PIE_enableInt(halObj->pieHandle, PIE_GroupNumber_5, PIE_InterruptSource_EQEP1);
	PIE_enableInt(halObj->pieHandle, PIE_GroupNumber_5, PIE_InterruptSource_EQEP2);

	// finally enable the CPU interrupt for PIE group 5 interrupts
	CPU_enableInt(halObj->cpuHandle, CPU_IntNumber_5);
}

