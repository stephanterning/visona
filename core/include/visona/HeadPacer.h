#pragma once

namespace visona
{

/**
    Paces the head the scope shows, so that it moves the same distance every frame (D-108).

    The analysis writes the sweep a device block at a time: with 512-frame blocks at 48 kHz, a
    block every 10.7 ms. A 60 Hz display that showed the head as written would show one block in
    some frames and two in others, so the head would step unevenly however steady the frames are.

    Against a line that moves at the sweep's speed, from its tempo, and slowly follows the head as
    written, that head is a staircase: furthest behind the moment before a block arrives. The
    paced head moves at the tempo and eases towards the line through the lowest steps, held for a
    while, less a quarter of the staircase's depth for steps lower still: about one block plus
    the analysis' polling, and a few ms, behind the newest audio. Following the head as written
    keeps a tempo that is slightly off from making the paced head drift away from it.

    When the head as written stops, as it does while the transport is stopped, the paced head goes
    on to it.

    Positions are in bins since the start of the sweep's first pass, so they carry on across the
    end of the window. One thread only; nothing allocates.
*/
class HeadPacer
{
public:
    /** A head as written further ahead than this is shown at once, without pacing. */
    static constexpr double maxLagSeconds = 0.25;

    /** Longer gaps between frames count as this long, so a stall does not make the head leap. */
    static constexpr double maxFrameSeconds = 0.1;

    /** A head as written that has not moved for this long has stopped. Longer than any device
        block. */
    static constexpr double stopSeconds = 0.15;

    /** The part of the distance to the head as written that the line covers in each frame. */
    static constexpr double lineEasing = 0.02;

    /** How fast the held lowest and highest steps let go, as a fraction of the tempo: 0.5 ms per
        second, so that the rare deep step a late poll makes is still held when the next one
        comes. */
    static constexpr double stepRelease = 0.0005;

    /** How far below the lowest step the paced head aims, as a fraction of the staircase's
        depth. */
    static constexpr double margin = 0.25;

    /** The part of the distance to its aim that the paced head covers in each frame. */
    static constexpr double easing = 0.2;

    /** Forgets the paced head: the next update shows the head as written. For a new stream or a
        cleared sweep, or after a jump. */
    void reset() noexcept
    {
        started_ = false;
    }

    /**
        The position to show at `seconds` when the sweep is written up to `written`, and moves at
        `binsPerSecond` while running, or 0 if that is not known. The result is never beyond
        `written` and, until reset(), never behind the previous one.
    */
    double update(double seconds, double written, double binsPerSecond) noexcept;

private:
    void restartLine(double written) noexcept;

    bool started_ = false;
    double shown_ = 0.0;
    double lastSeconds_ = 0.0;
    double lastWritten_ = 0.0;
    double lastMoveSeconds_ = 0.0;

    // The line, and the held lowest and highest steps of the head as written against it.
    double line_ = 0.0;
    double lowest_ = 0.0;
    double highest_ = 0.0;
};

} // namespace visona
