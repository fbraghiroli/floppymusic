#ifndef FM_DRIVEMANAGER_HPP
#define FM_DRIVEMANAGER_HPP

#include "DriveConfig.hpp"
#include <pthread.h>
#include <vector>

struct Drive
{
	int dir_fd;
	int step_fd;
    int direction_pin;
    int stepper_pin;
    int ticks;
    int maxticks;
    int steps;
    bool direction;
};
typedef std::vector<Drive> Drives;

class DriveManager
{
    private:
    bool m_running;
    Drives m_drives;
    pthread_t m_thread;
    pthread_mutex_t m_mutex;
    int gpioExport(int pin);
    int gpioUnexport(int pin);
    int gpioIsExported(int pin);
    int gpioConfigure(int pin);
    int gpioOpenFd(int pin, int *fd);

    public:
    DriveManager();
    DriveManager(DriveList drives);
    ~DriveManager();

    void loop();
    void setup();
    void play(int drive, double freq);
    void stop(int drive);
};

#endif
