#pragma once

// Starting points for the pattern maker. They ship as tables in code rather
// than embedded XML: five presets are a dozen numbers each, a typo in a
// parameter id fails to compile, the values are reviewable in a diff, and
// there is no binary-data target to keep in sync across three build targets.
//
// Plain arrays rather than std::initializer_list members: an initializer_list
// stored in a struct outlives its backing array, which dangles the moment the
// full expression ends.
//
// Any id left out of a table takes its parameter default.
struct PresetValue
{
    const char* id;
    float value;
};

struct FactoryPreset
{
    const char* name;
    const char* intent; // what to listen for when tuning it
    const PresetValue* values;
    int numValues;
};

int numFactoryPresets();
const FactoryPreset& factoryPreset (int index);
