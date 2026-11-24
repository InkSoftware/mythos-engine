// Built-in English and Greek strings for the engine's own screens (the settings
// menu). Greek text is UTF-8.
#include "engine/data/i18n.h"

namespace mythos {
namespace {
const TranslationTable kEnglish = {
    {"settings.title", "SETTINGS"},
    {"settings.graphics", "GRAPHICS"},
    {"settings.resolution", "RESOLUTION"},
    {"settings.desktop", "DESKTOP"},
    {"settings.renderScale", "RENDER SCALE"},
    {"settings.low", "LOW"},
    {"settings.medium", "MEDIUM"},
    {"settings.high", "HIGH"},
    {"settings.veryLow", "VERY LOW"},
    {"settings.veryHigh", "VERY HIGH"},
    {"settings.ultra", "ULTRA"},
    {"settings.max", "MAX"},
    {"settings.ultraMax", "ULTRA MAX"},
    {"settings.filmGrain", "FILM GRAIN"},
    {"settings.fullscreen", "FULLSCREEN"},
    {"settings.vsync", "VSYNC"},
    {"settings.antiAliasing", "ANTI-ALIASING"},
    {"settings.fpsCap", "FRAME RATE LIMIT"},
    {"settings.unlimited", "UNLIMITED"},
    {"settings.shadows", "SHADOWS"},
    {"settings.materials", "MATERIALS"},
    {"settings.basic", "BASIC"},
    {"settings.full", "FULL"},
    {"settings.motionBlur", "MOTION BLUR"},
    {"settings.visualFx", "VISUAL EFFECTS"},
    {"settings.ambientOcclusion", "AMBIENT OCCLUSION"},
    {"settings.cameraEffect", "CAMCORDER"},
    {"settings.audio", "AUDIO"},
    {"settings.masterVolume", "MASTER VOLUME"},
    {"settings.ambience", "AMBIENCE"},
    {"settings.effects", "EFFECTS"},
    {"settings.language", "LANGUAGE"},
    {"settings.back", "BACK"},
    {"settings.hint", "W/S SELECT   A/D CHANGE   ESC BACK"},
    {"common.on", "ON"},
    {"common.off", "OFF"},
};

const TranslationTable kGreek = {
    {"settings.title", "ΡΥΘΜΙΣΕΙΣ"},
    {"settings.graphics", "ΓΡΑΦΙΚΑ"},
    {"settings.resolution", "ΑΝΑΛΥΣΗ"},
    {"settings.desktop", "ΟΠΩΣ Η ΟΘΟΝΗ"},
    {"settings.renderScale", "ΚΛΙΜΑΚΑ ΑΠΟΔΟΣΗΣ"},
    {"settings.low", "ΧΑΜΗΛΗ"},
    {"settings.medium", "ΜΕΣΑΙΑ"},
    {"settings.high", "ΥΨΗΛΗ"},
    {"settings.veryLow", "ΠΟΛΥ ΧΑΜΗΛΗ"},
    {"settings.veryHigh", "ΠΟΛΥ ΥΨΗΛΗ"},
    {"settings.ultra", "ΥΠΕΡ"},
    {"settings.max", "ΜΕΓΙΣΤΗ"},
    {"settings.ultraMax", "ΥΠΕΡ ΜΕΓΙΣΤΗ"},
    {"settings.filmGrain", "ΚΟΚΚΟΣ ΦΙΛΜ"},
    {"settings.fullscreen", "ΠΛΗΡΗΣ ΟΘΟΝΗ"},
    {"settings.vsync", "VSYNC"},
    {"settings.antiAliasing", "ΕΞΟΜΑΛΥΝΣΗ ΑΚΜΩΝ"},
    {"settings.fpsCap", "ΟΡΙΟ ΚΑΡΕ"},
    {"settings.unlimited", "ΑΠΕΡΙΟΡΙΣΤΟ"},
    {"settings.shadows", "ΣΚΙΕΣ"},
    {"settings.materials", "ΥΛΙΚΑ"},
    {"settings.basic", "ΒΑΣΙΚΑ"},
    {"settings.full", "ΠΛΗΡΗ"},
    {"settings.motionBlur", "ΘΟΛΩΜΑ ΚΙΝΗΣΗΣ"},
    {"settings.visualFx", "ΟΠΤΙΚΑ ΕΦΕ"},
    {"settings.ambientOcclusion", "ΣΚΙΑΣΗ ΓΩΝΙΩΝ"},
    {"settings.cameraEffect", "ΒΙΝΤΕΟΚΑΜΕΡΑ"},
    {"settings.audio", "ΗΧΟΣ"},
    {"settings.masterVolume", "ΓΕΝΙΚΗ ΕΝΤΑΣΗ"},
    {"settings.ambience", "ΑΤΜΟΣΦΑΙΡΑ"},
    {"settings.effects", "ΕΦΕ"},
    {"settings.language", "ΓΛΩΣΣΑ"},
    {"settings.back", "ΠΙΣΩ"},
    {"settings.hint", "W/S ΕΠΙΛΟΓΗ   A/D ΑΛΛΑΓΗ   ESC ΠΙΣΩ"},
    {"common.on", "ΝΑΙ"},
    {"common.off", "ΟΧΙ"},
};
}

const TranslationTable& EngineStrings(const Lang l) {
  return l == Lang::kGreek ? kGreek : kEnglish;
}
}
