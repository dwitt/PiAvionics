#ifndef LOWPASSFILTER_H
#define LOWPASSFILTER_H

class LowPassFilter {
public:
    LowPassFilter(float alpha);

    float update(float input);
    float getValue() const;
    void reset() { _initialized = false; }   // next update() starts from its input

private:
    float _alpha;
    float _filteredValue;
    bool _initialized;
};

#endif