/* $Id$ */
//
// Проект HF Dream Receiver (КВ приёмник мечты)
// автор Гена Завидовский mgs2001@mail.ru
// UA1ARN
//

#include "hardware.h"
#include "board.h"
#include "bootloader.h"
#include "formats.h"


/* вызывается при запрещённых прерываниях. */
void
lowinitialize(void)
{
#if ! WITHRTOS
	//board_beep_initialize();
	//hardware_cw_diagnostics_noirq(1, 0, 1);	// 'K'
#if WITHDEBUG

	//HARDWARE_DEBUG_INITIALIZE();
	//HARDWARE_DEBUG_SET_SPEED(DEBUGSPEED);


	dbg_puts_impl_P(PSTR("Version " __DATE__ " " __TIME__ " 1 debug session starts.\n"));
	// выдача повторяющегося символа для тестирования скорости передачи, если ошибочная инициализация
	//for (;;)
	//	hardware_putchar(0xff);
	// тестирование приёма и передачи символов
	for (;0;)
	{
		char c;
		if (dbg_getchar(& c))
		{
			if (c == 0x1b)
				break;
			dbg_putchar(c);
		}
	}
#endif /* WITHDEBUG */

	//hardware_cw_diagnostics_noirq(1, 0, 0);	// 'D'
	// Инициализация таймера и списка регистрирумых обработчиков
	hardware_timer_initialize(TICKS_FREQUENCY);

	board_initialize();		/* инициализация чипселектов и SPI, I2C, загрузка FPGA */
	cpu_initdone();			/* секция init (в которой лежит образ для загрузки в FPGA) больше не нужна */
#endif /* ! WITHRTOS */
}

/* Главная функция программы */
int 
//__attribute__ ((used))
main(void)
{
#if LINUX_SUBSYSTEM
	linux_subsystem_init();
#endif /* LINUX_SUBSYSTEM */
#if (CPUSTYLE_ARM || CPUSTYLE_RISCV) && ! LINUX_SUBSYSTEM
	sysinit_gpio_initialize();
#endif /* (CPUSTYLE_ARM || CPUSTYLE_RISCV) && ! LINUX_SUBSYSTEM */
#if WITHDEBUG && (! CPUSTYLE_ARM /* || WITHISBOOTLOADER */)

	HARDWARE_DEBUG_INITIALIZE();
	HARDWARE_DEBUG_SET_SPEED(DEBUGSPEED);

#endif /* WITHDEBUG && ! CPUSTYLE_ARM */

	global_disableIRQ();
	cpu_initialize();		// в случае ARM - инициализация прерываний и контроллеров, AVR - запрет JTAG
	lowinitialize();	/* вызывается при запрещённых прерываниях. */
	global_enableIRQ();
	cpump_runuser();	/* остальным ядрам разрешаем выполнять прерывания */



	for (;;)
	{
		char c;
		if (dbg_getchar(& c))
		{
			dbg_putchar(c);
		}
	}


	return 0;
}
