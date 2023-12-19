
 /*
     ChibiOS - Copyright (C) 2006..2018 Giovanni Di Sirio

     Licensed under the Apache License, Version 2.0 (the "License");
     you may not use this file except in compliance with the License.
     You may obtain a copy of the License at

         http://www.apache.org/licenses/LICENSE-2.0

     Unless required by applicable law or agreed to in writing, software
     distributed under the License is distributed on an "AS IS" BASIS,
     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
     See the License for the specific language governing permissions and
     limitations under the License.
 */

 #include "ch.h"
 #include "hal.h"

 #define PORTAB_LINE_LED1 PAL_LINE(GPIOB, 14U)
 #define PORTAB_LINE_LED2 PAL_LINE(GPIOB, 13U)
 #define PORTAB_LED_OFF   PAL_HIGH
 #define PORTAB_LED_ON    AL_LOW

 #define PORTAB_LINE_BUTTON    PAL_LINE(GPIOA, 0U)
 #define PORTAB_BUTTON_PRESSED PAL_LOW

 RTCDateTime timespec;
 RTCAlarm alarmspec;



#define TEST_ALARM_WAKEUP     FALSE

#if TEST_ALARM_WAKEUP

/*
 * Running indicator thread.
 */
static THD_WORKING_AREA(blinkWA, 128);
static THD_FUNCTION(blink_thd, arg) {
  (void)arg;
  while (true) {
    chThdSleepMilliseconds(100);
    palToggleLine(PORTAB_LINE_LED2);
  }
}
static void hal_lld_backup_domain_init(void) {

  /* Turn on the backup domain clock.*/
  rccEnableBKPInterface();

#if HAL_USE_RTC
  /* Reset BKP domain if different clock source selected.*/
  if ((BKP->BDCR & WB32_RTCSEL_MASK) != WB32_RTCSEL) {
    /* Backup domain reset.*/
    RCC->BDRSTR = 0x1U;
    RCC->BDRSTR = 0x0U;
  }

  /* If enabled then the LSE is started.*/
#if WB32_LSE_ENABLED
#if defined(WB32_LSE_BYPASS)
  /* LSE Bypass.*/
  BKP->BDCR = RCC_BDCR_LSEON;
#else
  /* No LSE Bypass.*/
  BKP->BDCR = BKP_LSE_Bypass;
#endif
  while ((BKP->BDCR & 0x2U) == 0)
    ;                                     /* Waits until LSE is stable.   */
#endif /* WB32_LSE_ENABLED */

#if WB32_RTCSEL != WB32_RTCSEL_NOCLOCK
  /* If the backup domain hasn't been initialized yet then proceed with
     initialization.*/
  if ((RCC->BDRSTR & 0x1U) == 0) {
    /* Selects clock source.*/
    BKP->BDCR |= WB32_RTCSEL;

    /* Prescaler value loaded in registers.*/
    rtc_lld_set_prescaler();

    /* RTC clock enabled.*/
    BKP->BDCR |= 0x1U;
  }
#endif /* WB32_RTCSEL != WB32_RTCSEL_NOCLOCK */
#endif /* HAL_USE_RTC */
}

/*
 *
 */
static const uint32_t pre_lp_code[] = {553863175u,554459777u,1208378049u,4026624001u,688390415u,554227969u,3204472833u,1198571264u,1073807360u,1073808388u};
#define PRE_LP()  ((void(*)(void))((unsigned int)(pre_lp_code) | 0x01))()

static const uint32_t post_lp_code[] = {553863177u,554459777u,1208509121u,51443856u,4026550535u,1745485839u,3489677954u,536895496u,673389632u,1198578684u,1073807360u,536866816u,1073808388u};
#define POST_LP()  ((void(*)(void))((unsigned int)(post_lp_code) | 0x01))()
  
static void stop_mode_entry(void) {

/* 增加这部分代码会导致进不去低功耗模式 */
//   int state;
//   state = __get_PRIMASK();
//   __disable_irq();

#if 1
  EXTI->PR = 0x7FFFF;
  for (uint8_t i = 0; i < 8; i++) {
    for (uint8_t j = 0; j < 32; j++) {
      if (NVIC->ISPR[i] & (0x01UL < j)) {
        NVIC->ICPR[i] = (0x01UL < j);
      }
    }
  }
  SCB->ICSR |= SCB_ICSR_PENDSTCLR_Msk; // Clear Systick IRQ Pending
#endif

  /* Clear all bits except DBP and FCLKSD bit */
  PWR->CR0 &= 0x09U;

  // STOP LP4 MODE S32KON
  PWR->CR0 |= 0x3B004U;
  PWR->CFGR = 0x3B3;

  PRE_LP();

  /* Set SLEEPDEEP bit of Cortex System Control Register */
  SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;

  /* Request Wait For Interrupt */
  __WFI();

  POST_LP();

  /* Clear SLEEPDEEP bit of Cortex System Control Register */
  SCB->SCR &= (~SCB_SCR_SLEEPDEEP_Msk);

//   if (!state) {
//     __enable_irq();
//   }
}

  static void my_cb(RTCDriver *rtcp, rtcevent_t event) {

   (void)rtcp;

   switch (event) {
   case RTC_EVENT_OVERFLOW:

     break;
   case RTC_EVENT_SECOND:
     palToggleLine(PORTAB_LINE_LED2);
     break;
   case RTC_EVENT_ALARM:
     osalSysLockFromISR();
     EXTI->IMR &= ~EXTI_IMR_MR17;
     NVIC_DisableIRQ(RTCAlarm_IRQn);
     osalSysUnlockFromISR();
     break;
   }
  }
 
int main(void) {

  uint32_t tv_sec;

  halInit();
  chSysInit();

   /* 
    * Init LED port and pad.
    */
   palSetPadMode(PAL_PORT(PORTAB_LINE_LED1), PAL_PAD(PORTAB_LINE_LED1), PAL_WB32_MODE_OUTPUT | PAL_WB32_OTYPE_PUSHPULL);
   palSetPadMode(PAL_PORT(PORTAB_LINE_LED2), PAL_PAD(PORTAB_LINE_LED2), PAL_WB32_MODE_OUTPUT | PAL_WB32_OTYPE_PUSHPULL);
  
  chThdCreateStatic(blinkWA, sizeof(blinkWA), NORMALPRIO, blink_thd, NULL);

  /* compile ability test */
  rtcGetTime(&RTCD1, &timespec);
  
  /* set alarm in near future */
  rtcWB32GetSecMsec(&RTCD1, &tv_sec, NULL);
  alarmspec.tv_sec = tv_sec + 20;
  rtcSetAlarm(&RTCD1, 0, &alarmspec);
  rtcSetCallback(&RTCD1, my_cb);
  while (true){
    chThdSleepSeconds(10);

    chSysDisable();
    RTC->CRL &= ~(RTC_CRL_SECF | RTC_CRL_ALRF | RTC_CRL_OWF);
    EXTI->PR = 1 << 17;
    EXTI->IMR |= EXTI_IMR_MR17;
    NVIC_EnableIRQ(RTCAlarm_IRQn);
    
    stop_mode_entry();
 
    extern void __early_init(void);
    __early_init();

    rccEnableEXTI();

    rccEnableBKP();
    rccEnableBKPInterface();
  
    chSysEnable();
    
    rtcWB32GetSecMsec(&RTCD1, &tv_sec, NULL);
    alarmspec.tv_sec = tv_sec + 20;
    rtcSetAlarm(&RTCD1, 0, &alarmspec);
  }
}

#else /* TEST_ALARM_WAKEUP */

 /*
  * Test alarm period.
  */
 #define RTC_ALARMPERIOD   10

 binary_semaphore_t alarm_sem;

 /*
  * Alarm callback.
  */
 static void my_cb(RTCDriver *rtcp, rtcevent_t event) {

   (void)rtcp;

   switch (event) {
   case RTC_EVENT_OVERFLOW:

     break;
   case RTC_EVENT_SECOND:
     palToggleLine(PORTAB_LINE_LED2);
     break;
   case RTC_EVENT_ALARM:
     //palToggleLine(PORTAB_LINE_LED2);
     osalSysLockFromISR();
     chBSemSignalI(&alarm_sem);
     osalSysUnlockFromISR();
     break;
   }
 }

 static time_measurement_t sett, gett;

 int main(void) {

   msg_t status = MSG_TIMEOUT;
   uint32_t tv_sec;

   halInit();
   chSysInit();

   /* 
    * Init LED port and pad.
    */
   palSetPadMode(PAL_PORT(PORTAB_LINE_LED1), PAL_PAD(PORTAB_LINE_LED1), PAL_WB32_MODE_OUTPUT | PAL_WB32_OTYPE_PUSHPULL);
   palSetPadMode(PAL_PORT(PORTAB_LINE_LED2), PAL_PAD(PORTAB_LINE_LED2), PAL_WB32_MODE_OUTPUT | PAL_WB32_OTYPE_PUSHPULL);
  
   /* 
    * Init button port and pad.
    */
   palSetPadMode(PAL_PORT(PORTAB_LINE_BUTTON), PAL_PAD(PORTAB_LINE_BUTTON), PAL_WB32_MODE_INPUT | PAL_WB32_PUPDR_PULLDOWN);

    chBSemObjectInit(&alarm_sem, TRUE);
    chTMObjectInit(&sett);
    chTMObjectInit(&gett);

    /* compile ability test */
    chTMStartMeasurementX(&gett);
    rtcGetTime(&RTCD1, &timespec);
    chTMStopMeasurementX(&gett);

    rtcWB32SetSec(&RTCD1, 1414845464);
    osalThreadSleepMilliseconds(10);
    rtcGetTime(&RTCD1, &timespec);
    timespec.month -= 1;

    chTMStartMeasurementX(&sett);
    rtcSetTime(&RTCD1, &timespec);
    chTMStopMeasurementX(&sett);
    osalThreadSleepMilliseconds(10);

    rtcGetTime(&RTCD1, &timespec);

    rtcWB32GetSecMsec(&RTCD1, &tv_sec, NULL);
    alarmspec.tv_sec = tv_sec + RTC_ALARMPERIOD;
    rtcSetAlarm(&RTCD1, 0, &alarmspec);

    rtcSetCallback(&RTCD1, my_cb);

   while (true){
      /* Wait until alarm callback signaled semaphore.*/
      status = chBSemWaitTimeout(&alarm_sem, TIME_S2I(RTC_ALARMPERIOD + 5));

      if (status == MSG_TIMEOUT){
        osalSysHalt("time is out");
      }
      else{
        rtcWB32GetSecMsec(&RTCD1, &tv_sec, NULL);
        alarmspec.tv_sec = tv_sec + RTC_ALARMPERIOD;
        rtcSetAlarm(&RTCD1, 0, &alarmspec);
      }
   }
   return 0;
 }


#endif /* TEST_ALARM_WAKEUP */
