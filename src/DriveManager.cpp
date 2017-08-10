#include "DriveManager.hpp"
#include "gpio.hpp"
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#define MAX_STEPS 80
#define RESOLUTION 7200
#define SEC_IN_NSEC (1000000000)

DriveManager::DriveManager() : m_running(false)
{}


DriveManager::DriveManager(DriveList drives) : m_running(false)
{
    for (DriveList::iterator drv = drives.begin();
            drv != drives.end(); ++drv)
    {
        Drive d = {
		0, 0,
            drv->direction_pin,
            drv->stepper_pin,
            0, -1, 0, true};
        m_drives.push_back(d);
    }
}


DriveManager::~DriveManager()
{
    if (!m_running) return;
    m_running = false;
    pthread_join(m_thread, NULL);

#ifdef SYSFS
    for (Drives::iterator d = m_drives.begin();
         d != m_drives.end(); ++d) {
	    gpioUnexport(d->direction_pin);
	    gpioUnexport(d->stepper_pin);
    }
#endif

}


static void *_drive_jumper(void *drive)
{
    reinterpret_cast<DriveManager*>(drive)->loop();
    return NULL;
}


#ifndef FASTIO
#pragma GCC push_options
#pragma GCC optimize("O0")
/* I've wondered for days why one of my drives is working with some test
 * scripts (written in C and Python), but refuses to do anything when
 * used with floppymusic. Turns out that floppymusic is just too fast.
 * The overhead of the C output_gpio() calls or the Python calls were
 * enough to let the drive work.
 *
 * This function does nothing but waste some CPU cycles. It is called
 * between GPIO_SET and GPIO_CLR on the step pins. You can turn this
 * behaviour off by compiling with -DFASTIO (change CC_FLAGS in
 * Makefile accordingly).
 */
static void _nop_delay(void)
{
    for (int i = 15; i > 0; --i)
    {
        asm volatile("nop");
    }
}
#pragma GCC pop_options
#endif

int DriveManager::gpioExport(int pin)
{
	int fd;
	int ret = 0;
	char pin_str[5];

	if (gpioIsExported(pin))
		return ret;

	fd = open("/sys/class/gpio/export", O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "Failed to export pin %d (errno: %d)\n",
			pin, errno);
		return -errno;
	}

	snprintf(pin_str, 5, "%d", pin);
	if (write(fd, pin_str, strlen(pin_str)) < 0) {
		fprintf(stderr, "Failed to export pin %d (errno: %d)\n",
			pin, errno);
		ret = -errno;
	}
	close(fd);

	return ret;
}

int DriveManager::gpioUnexport(int pin)
{
	int fd;
	int ret = 0;
	char pin_str[5];

	if (!gpioIsExported(pin))
		return ret;

	fd = open("/sys/class/gpio/unexport", O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "Failed to export pin %d (errno: %d)\n",
			pin, errno);
		return -errno;
	}

	snprintf(pin_str, 5, "%d", pin);
	if (write(fd, pin_str, strlen(pin_str)) < 0) {
		fprintf(stderr, "Failed to unexport pin %d (errno: %d)\n",
			pin, errno);
		ret = -errno;
	}
	close(fd);

	return ret;
}

int DriveManager::gpioIsExported(int pin)
{
	char buf[30];
	snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d", pin);

	if (access(buf, F_OK) != -1)
		return 1;
	return 0;
}

int DriveManager::gpioConfigure(int pin)
{
	int fd;
	int ret = 0;
	char buf[40];

	snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d/direction", pin);
	fd = open(buf, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "Failed to configure pin %d (errno: %d)\n",
			pin, errno);
		return -errno;
	}
	if (write(fd, "out", 3) < 0)
		ret = -errno;
	close(fd);

	return ret;
}

int DriveManager::gpioOpenFd(int pin, int *fd)
{
	char buf[40];

	snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d/value", pin);
	*fd = open(buf, O_WRONLY);
	if (*fd < 0) {
		fprintf(stderr, "Failed to open %s (errno: %d)\n", buf, errno);
		return -errno;
	}

	return 0;
}

void DriveManager::setup()
{
    if (m_running) return;
    for (Drives::iterator d = m_drives.begin();
            d != m_drives.end(); ++d)
    {
#ifdef SYSFS
	    if (gpioExport(d->direction_pin) < 0 ||
		gpioConfigure(d->direction_pin) < 0)
		    return;
	    if (gpioExport(d->stepper_pin) < 0 ||
		gpioConfigure(d->stepper_pin) < 0)
		    return;
	    if (gpioOpenFd(d->direction_pin, &d->dir_fd) < 0)
		    return;
	    if (gpioOpenFd(d->stepper_pin, &d->step_fd) < 0)
		    return;
#else
        // Always use INP before OUT
        INP_GPIO(d->direction_pin);
        INP_GPIO(d->stepper_pin);
        OUT_GPIO(d->direction_pin);
        OUT_GPIO(d->stepper_pin);
#endif
        // "reseed" the drive
#ifndef NOGPIO
#ifdef SYSFS
	write(d->dir_fd, "0", 1);
#else
        GPIO_CLR = 1 << d->direction_pin;
#endif
#endif
        for (int i=0; i<MAX_STEPS; ++i)
        {
#ifndef NOGPIO
#ifdef SYSFS
            write(d->step_fd, "1", 1);
#else
            GPIO_SET = 1 << d->stepper_pin;
#endif
#ifndef FASTIO
            _nop_delay();
#endif
#ifdef SYSFS
            write(d->step_fd, "0", 1);
#else
            GPIO_CLR = 1 << d->stepper_pin;
#endif

#endif
            usleep(2500);
        }
#ifndef NOGPIO
#ifdef SYSFS
            write(d->dir_fd, "1", 1);
#else
            GPIO_SET = 1 << d->direction_pin;
#endif
#endif
    }
    pthread_mutex_init(&m_mutex, NULL);
    pthread_create(&m_thread, NULL, _drive_jumper, this);
    m_running = true;
}


void DriveManager::loop()
{
    timespec t;
    unsigned long nsec = SEC_IN_NSEC / RESOLUTION;
    t.tv_sec = nsec / SEC_IN_NSEC;
    t.tv_nsec = nsec % SEC_IN_NSEC;
    while (m_running)
    {
        pthread_mutex_lock(&m_mutex);
        for (Drives::iterator d = m_drives.begin();
                d != m_drives.end(); ++d)
        {
            if (d->maxticks == -1) continue;
            ++d->ticks;
            if (d->ticks >= d->maxticks)
            {
                // should do a step
                // need to reverse direction first?
                ++d->steps;
                if (d->steps > MAX_STEPS)
                {
                    d->direction = !d->direction;
#ifndef NOGPIO
                    if (d->direction)
                    {
#ifdef SYSFS
                        write(d->dir_fd, "1", 1);
#else
                        GPIO_SET = 1 << d->direction_pin;
#endif
                    }
                    else 
                    {
#ifdef SYSFS
                        write(d->dir_fd, "0", 1);
#else
                        GPIO_CLR = 1 << d->direction_pin;
#endif
                    }
#endif
                    d->steps = 0;
                }
                // now send a pulse
#ifndef NOGPIO
#ifdef SYSFS
                write(d->step_fd, "1", 1);
#else
                GPIO_SET = 1 << d->stepper_pin;
#endif
#ifndef FASTIO
                // See definition of _nop_delay for more information
                _nop_delay();
		//usleep(50);
#endif
#ifdef SYSFS
                write(d->step_fd, "0", 1);
#else
                GPIO_CLR = 1 << d->stepper_pin;
#endif
#endif
                d->ticks = 0;
            }
        }
        pthread_mutex_unlock(&m_mutex);
        nanosleep(&t, NULL);
    }
}


void DriveManager::play(int drive, double frequency)
{
    if (frequency == 0)
    {
        this->stop(drive);
        return;
    }
    pthread_mutex_lock(&m_mutex);
    Drive& d = m_drives[drive];
    d.ticks = 0;
    d.maxticks = RESOLUTION / frequency;
    pthread_mutex_unlock(&m_mutex);
}


void DriveManager::stop(int drive)
{
    m_drives[drive].maxticks = -1;
}
