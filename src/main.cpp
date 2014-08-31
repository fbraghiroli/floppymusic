#include "Arguments.hpp"
#include "DriveConfig.hpp"
#include "DriveManager.hpp"
#include "MidiEvents.hpp"
#include "MidiFile.hpp"
#include "MidiTrack.hpp"
#include "gpio.hpp"
#include "version.hpp" // generated by Makefile
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <map>
#include <unistd.h>
#include <vector>


// C C# D D# E F F# G G# A A# H
static double frequencies[] = {261.626, 277.183, 293.665, 311.127, 329.628,
                               349.228, 369.994, 391.995, 415.305, 440.000,
                               466.164, 493.883};


/* Convert carriage-return characters in a string to newline chars.
 * Creates a copy of the string and returns the modified copy.
 */
static std::string r_to_n(std::string s)
{
    size_t it = 0;
    while ((it = s.find("\r", it)) != std::string::npos)
    {
        s.replace(it, 1, "\n");
    }
    return s;
}


typedef std::vector<Drive*> vDrive;
int main(int argc, char **argv)
{
    std::cout << "[floppymusic " << FM_VERSION << "]" << std::endl;
    parse_args(argc, argv);

    std::cout << "Reading drive configuration " << arguments.cfg_path
        << std::endl;   
    std::ifstream dc_file(arguments.cfg_path.c_str());
    if (!dc_file.good())
    {
        std::cerr << "Can't open " << arguments.cfg_path << ": "
            << std::strerror(errno) << std::endl;
        return 1;
    }

    DriveConfig drive_cfg(dc_file);
    if (!drive_cfg.isValid())
    {
        std::cerr << "Invalid drive configuration. Aborting." << std::endl;
        return 1;
    }

    std::cout << "Setting up GPIO" << std::endl;
    setup_io();

    std::cout << "Setting up drives" << std::endl;
    DriveList drive_list = drive_cfg.getDrives();
    DriveManager dmgr = DriveManager(drive_list);
    dmgr.setup();
    int dcount = drive_list.size();

    std::cout << "Reading MIDI file" << std::endl;
    std::ifstream midi_input(arguments.midi_path.c_str());
    if (!midi_input.good())
    {
        std::cerr << "Error reading '" << arguments.midi_path << "': "
            << std::strerror(errno) << std::endl;
        return 1;
    }
    MidiFile midi;
    if (!midi.read(midi_input))
    {
        std::cerr << "Invalid MIDI File. Aborting." << std::endl;
        return 1;
    }
    if (midi.getFormatType() == 2)
    {
        std::cerr << "This is a MIDI file of type 2 and not supported "
            "(yet) by floppymusic :(" << std::endl;
        return 1;
    }

    std::cout << "Merging " << (int)midi.getTrackCount() << " tracks"
        << std::endl;
    EventList track = midi.mergedTracks(arguments.mute_tracks);
    std::cout << "Ready, steady, go!" << std::endl;
    std::map<int, int> channel_map;
    std::map<int, int>::iterator drive_index;
    int pool_free = 0;
    int new_index = -1;

    /* Play loop */
    for (EventList::iterator event = track.begin();
            event != track.end(); ++event)
    {
        // Praise usleep
        if ((*event)->relative_musec)
        {
            usleep((*event)->relative_musec);
        }
        if ((*event)->type() == Event_Note_Off)
        {
            NoteOffEvent* e = dynamic_cast<NoteOffEvent*>(*event);
            if (e->muted) continue;
            // Stop playing and release the drive back to the pool
            drive_index = channel_map.find(e->getChannel());
            if (drive_index != channel_map.end())
            {
                dmgr.stop(drive_index->second);
                pool_free ^= 1 << drive_index->second;
                channel_map.erase(drive_index);
            }
        }
        else if ((*event)->type() == Event_Note_On)
        {
            NoteOnEvent* e = dynamic_cast<NoteOnEvent*>(*event);
            if (e->muted) continue;
            new_index = -1;
            // See if the drive is already reserved
            drive_index = channel_map.find(e->getChannel());
            if (drive_index != channel_map.end())
            {
                new_index = drive_index->second;
            }
            else
            {
                // See if a drive is free
                for (int check = 0; check < dcount; ++check)
                {
                    if (!(pool_free & (1 << check)))
                    {
                        // Device is free
                        new_index = check;
                        break; // stop searching for a drive
                    }
                }
            }
            if (new_index != -1)
            {

                channel_map[e->getChannel()] = new_index;
                dmgr.play(new_index,
                    frequencies[e->getNote() % 12] / arguments.drop_factor);
                pool_free |= 1 << new_index;
            }
        }
        else if (arguments.lyrics && (*event)->type() == Event_Lyrics)
        {
            LyricsEvent* e = dynamic_cast<LyricsEvent*>(*event);
            std::cout << r_to_n(e->getText()) << std::flush;
        }
    }

    std::cout << "Cleaning up" << std::endl;
    std::cout << "Bye bye!" << std::endl;
}
