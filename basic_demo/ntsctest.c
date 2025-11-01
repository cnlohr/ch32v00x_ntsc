// Stream a continuous stream of values to Timer 1 Output 1

#include "ch32fun.h"
#include <stdio.h>


#define RANDOM_STRENGTH 1

#include "lib_rand.h"

#define DMAHW DMA1_Channel5
#define DMAHW_IRQn DMA1_Channel5_IRQn
#define DMAHW_IRQHandler DMA1_Channel5_IRQHandler
#define DMAHWGL DMA1_IT_GL5
#define DMAHWTC DMA1_IT_TC5
#define DMAHWHT DMA1_IT_HT5

#ifdef CH32V003
// Leave in flash on 003, it's faster
#define LOCATION_DECORATOR
#define TIM1_SWEVGR_UG      TIM_UG
#define TIM1_CCER_CC1E      TIM_CC1E
#define TIM1_CHCTLR1_OC1M_2 TIM_OC1M_2
#define TIM1_CHCTLR1_OC1M_1 TIM_OC1M_1
#define TIM1_BDTR_MOE       TIM_MOE
#define TIM1_DMAINTENR_UDE  TIM_UDE
#define TIM1_DMAINTENR_COMDE TIM_COMDE
#define TIM1_CTLR1_CEN       TIM_CEN
#else
// Put in RAM on the 006, it's faster
#define LOCATION_DECORATOR __attribute__((section(".srodata")))
#endif

#define MBSAMPS 256
volatile uint8_t memory_buffer[MBSAMPS];

int frame;

#define RES_W 40


	// steps minus 1, at 1.2MHz, value
	// H = 63.555562561 us
	// H = 76.266675073 steps.

	// p = 2.82 steps
	// q = 32.52 steps
	// r = 5.64 steps
	// p' = 35.31 steps

const uint8_t synctable1half[] = {
	4, 0,  // q
	26, 1,   // r
	0, 0,
};

const uint8_t synctable12[] = {
	4, 0,  // q
	26, 1,   // r
	4, 0,  // q
	26, 1,   // r
	0, 0,
};

const uint8_t synctable3[] = {
	26, 0, // q
	4, 1,  // r
	4, 0,  // p
	26, 1, // p'
	0, 0,
};

const uint8_t synctable45[] = {
	26, 0, // p'
	4, 1,  // p
	26, 0, // p'
	4, 1,  // p
	0, 0,
};

const uint8_t synctableblank[] = {
	4, 0, // r
	19, 1, // r'
	18, 1, // r'
	19, 1, // r'
	0, 0,
};

const uint8_t synctableline[] = {
	4, 0,
	9, 1,
	RES_W, 2, // frame data
	47-RES_W, 1,
	0, 0,
};

const uint8_t synctable3B[] = {
	4, 0,
	26, 1,
	26, 0,
	4, 1,
	0, 0,
};


const uint8_t synctable6B[] = {
	25, 0,
	5, 1,
	3, 0,
	27, 1,
	0, 0,
};


const uint32_t synctable_map[] = {
// First field.
	(uintptr_t)synctable12, 3,
	(uintptr_t)synctable45, 3,
	(uintptr_t)synctable12, 3,
	(uintptr_t)synctableblank, 18, // +3 blank lines just cause.
	(uintptr_t)synctableline, 235,
	// 263 is a wacky one...  Just pretend it's like 1 and 2.
	(uintptr_t)synctable12, 3,
	// Second field, part 3.
	(uintptr_t)synctable3B, 1, 
	(uintptr_t)synctable45, 2,
	(uintptr_t)synctable6B, 1,
	(uintptr_t)synctable12, 2, // 314, 315
	(uintptr_t)synctableblank, 17,
	(uintptr_t)synctableline, 234,
//	(uintptr_t)synctable1half, 1,

	0, 0
};


static void DataFill( uint32_t * o, int words ) LOCATION_DECORATOR;
static void DataFill( uint32_t * o, int words )
{
	// TODO: Sane starting values.
	static const uint32_t * stable = &synctable_map[0];
	static int stablecnt = 1;
	static const uint8_t * ctable_s = &synctable12[0];
	static int ctablecnt_s = 1;
	static int ctablevalue_s = 0;

	const uint8_t * ctable = ctable_s;
	int ctablecnt;
	int ctablevalue;

	ctablecnt = ctablecnt_s;
	ctablevalue = ctablevalue_s;

	uint32_t * oend = o + words;
	do
	{
		if( ctablecnt-- == 0 )
		{
outer_loop:
			ctablecnt = ctable[0];
			ctablevalue = ctable[1];
			ctable += 2;

			if( ctablecnt == 0 )
			{

				if( stablecnt-- == 0 )
				{
inner_loop:
					stable+=2;
					ctable = (uint8_t*)stable[0];
					stablecnt = stable[1];

					if( ctable == 0 )
					{
						stable = synctable_map-2;
						frame++;
						goto inner_loop;
					}
				}
				else
				{
					ctable = (uint8_t*)stable[0];
				}
				goto outer_loop;
			}
		}

		if( ctablevalue == 0 )
			*o = 0;
		else if( ctablevalue == 1 )
			*o = 0x02020202;
		else
		{
			//if( ctable != synctableline + 6 )
			//	printf( "%08x %08x %08x\n", (unsigned int)stable, (unsigned int)ctable, (unsigned int)synctableline );
//			if( (stablecnt&3) == 0 )
//				*o = 0x06060606;
//			else
//				*o = (ctablecnt&1)?0x06020202:0x06020202;
			int on = ((uint8_t)(( (frame-ctablecnt) & (frame-(stablecnt>>3)))))>0;
			*o = (on)?0x06060606:0x02020202;

		}
	} while( (++o) != oend );

	ctable_s = ctable;
	ctablecnt_s = ctablecnt;
	ctablevalue_s = ctablevalue;
}

void DMAHW_IRQHandler( void ) __attribute__((interrupt)) LOCATION_DECORATOR;
void DMAHW_IRQHandler( void )
{
	static int frameno;
	volatile int intfr = DMA1->INTFR;
	do
	{
		DMA1->INTFCR = DMAHWGL;

		// Gets called at the end-of-a frame.
		if( intfr & DMAHWTC )
		{
			uint32_t * mbb = (uint32_t*)( memory_buffer + MBSAMPS/2 );
			DataFill( mbb, MBSAMPS/8 );
			frameno++;
		}

		// Gets called halfway through the frame
		if( intfr & DMAHWHT )
		{
			uint32_t * mbb = (uint32_t*)( memory_buffer );
			DataFill( mbb, MBSAMPS/8 );
		}
		intfr = DMA1->INTFR;
	} while( intfr );
}

int main()
{
	SystemInit();

	// Using a DMA to feed TIM1CH1 (GPIO PD2)

	// Enable GPIOD and TIM1
	// Enable DMA

	// Target 65.337us per frame segment.
	//RCC->CTLR = (RCC->CTLR & 0xffffff03) | 0x80;


	RCC->AHBPCENR |= RCC_AHBPeriph_DMA1;
	RCC->APB2PCENR |= RCC_APB2Periph_GPIOD | RCC_APB2Periph_TIM1 | RCC_APB2Periph_AFIO;
	
	funPinMode( PD2, GPIO_CFGLR_OUT_2Mhz_AF_PP );
	funPinMode( PD3, GPIO_CFGLR_OUT_50Mhz_PP );

	// No remapping (But you could remap if you want)
	AFIO->PCFR1 = 0;

	// Reset TIM1 to init all regs
	RCC->APB2PRSTR |= RCC_APB2Periph_TIM1;
	RCC->APB2PRSTR &= ~RCC_APB2Periph_TIM1;

	// Prescaler 
	TIM1->PSC = 0x0000;
	
	// Auto Reload - sets period
	TIM1->ATRLR = 11; // 4.8MHz (0..9)

	// Reload immediately
	TIM1->SWEVGR |= TIM1_SWEVGR_UG;
	
	// Enable CH1N output, positive pol
	TIM1->CCER |= TIM1_CCER_CC1E;// | TIM_CC1P;
	
	// CH1 Mode is output, PWM1 (CC1S = 00, OC1M = 110)
	TIM1->CHCTLR1 |= TIM1_CHCTLR1_OC1M_2 | TIM1_CHCTLR1_OC1M_1;

	// Set the Capture Compare Register value to 50% initially
	TIM1->CH1CVR = 0;
	
	// Enable TIM1 outputs (You only need to do this on tim1)
	TIM1->BDTR |= TIM1_BDTR_MOE;

// Syncrhonization mode.
#ifdef CH32V003
	TIM1->CTLR2 =
		TIM_MMS_0 | 
	//	TIM_MMS_1 | 
		//TIM_MMS_2 | 
		0;
#else
	TIM1->CTLR2 =
		TIM1_CTLR2_MMS_1 | 
		TIM1_CTLR2_MMS_0 | 
		//TIM1_CTLR2_MMS_2 | 
		0;
#endif

	// Enable TIM1
	TIM1->CTLR1 |= TIM1_CTLR1_CEN;

	// DMA2 can be configured to attach to T1CH1, Channel 2

	DMAHW->CNTR = sizeof(memory_buffer) / sizeof(memory_buffer[0]);
	DMAHW->MADDR = (uint32_t)memory_buffer;
	DMAHW->PADDR = (uint32_t)&TIM1->CH1CVR; // This is the output register for out buffer.
	DMAHW->CFGR =
		DMA_CFGR1_DIR |                      // MEM2PERIPHERAL
		DMA_CFGR1_PL |                       // High priority.
		0 |                                  // 8-bit memory
		DMA_CFGR1_PSIZE_0 |                  // 32-bit peripheral
		DMA_CFGR1_MINC |                     // Increase memory.
		DMA_CFGR1_CIRC |                     // Circular mode.
		DMA_CFGR1_HTIE |                     // Half-trigger
		DMA_CFGR1_TCIE |                     // Whole-trigger
		DMA_CFGR1_EN;                        // Enable

	NVIC_EnableIRQ( DMAHW_IRQn );
	DMAHW->CFGR |= DMA_CFGR1_EN;

//	TIM1->DMAINTENR = TIM_TDE | TIM_CC1DE;// | TIM_UDE; (For channel 2)
	TIM1->DMAINTENR = TIM1_DMAINTENR_UDE | TIM1_DMAINTENR_COMDE; //(For channel 5)

	while(1)
	{
		funDigitalWrite( PD3, FUN_HIGH );
		funDigitalWrite( PD3, FUN_LOW );
	}
}
