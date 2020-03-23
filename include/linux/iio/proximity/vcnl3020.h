#define VCNL_DRV_NAME		"vcnl3020"
#define VCNL_DRV_HWMON		"vcnl3020-hwmon"
#define VCNL3020_PROD_ID	0x21

#define VCNL_COMMAND		0x80 /* Command register */
#define VCNL_PROD_REV		0x81 /* Product ID and Revision ID */
#define VCNL_PROXIMITY_RATE	0x82 /* Rate of Proximity Measurement */
#define VCNL_LED_CURRENT	0x83 /* IR LED current for proximity mode */
#define VCNL_PS_RESULT_HI	0x87 /* Proximity result register, MSB */
#define VCNL_PS_RESULT_LO	0x88 /* Proximity result register, LSB */
#define VCNL_PS_ICR		0x89 /* Interrupt Control Register  */
#define    ICR_THRES_EN		0x02 /* Enable interrupts on low or high
					thresholds */
#define    ICR_COUNT_EXCEED	0x10 /* Measurements above/below thresholds */

#define VCNL_PS_LO_THR_HI	0x8a /* High byte of low threshold value */
#define VCNL_PS_LO_THR_LO	0x8b /* Low byte of low threshold value */
#define VCNL_PS_HI_THR_HI	0x8c /* High byte of high threshold value */
#define VCNL_PS_HI_THR_LO	0x8d /* Low byte of high threshold value */
#define VCNL_ISR		0x8e /* Interrupt Status Register */
#define   INT_TH_HI		0
#define   INT_TH_LOW		1
#define   INT_PROX_READY	3
#define VCNL_PS_MOD_ADJ		0x8f /* Proximity Modulator Timing Adjustment */

/* Bit masks for COMMAND register */
#define VCNL_PS_RDY		BIT(5) /* proximity data ready? */
#define VCNL_PS_OD		BIT(3) /* start on-demand proximity measurement */
#define VCNL_PS_EN		BIT(1)
#define VCNL_PS_SELFTIMED_EN	BIT(0)

struct vcnl3020_data {
	struct i2c_client *client;
	int32_t rev;
	struct mutex vcnl3020_lock;
};

bool vcnl3020_intrusion(struct vcnl3020_data *data);
int32_t vcnl3020_clear_interrupts(struct vcnl3020_data *client);

