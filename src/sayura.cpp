/*
 * SPDX-FileCopyrightText: 2012~2012 Yichao Yu
 * SPDX-FileCopyrightText: 2020~2020 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#include "sayura.h"
#include <deque>
#include <fcitx-utils/log.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>

namespace fcitx {

struct AddendumCharacter {
    uint32_t baseChar;
    uint32_t secondaryChar; // Aspirate or Diacritic, if there's an aspirate it would be 0x001
};

struct AddendumCharMapping {
    AddendumCharacter character;
    std::unordered_map<KeySym, AddendumCharacter> leadingModCharMappings;
};

struct AddendumVowelMapping {
    uint32_t baseChar;
    uint32_t diacritic;
    std::unordered_map<KeySym, AddendumCharacter> leadingModCharMappings;
};

enum VowelStatus {
    CAN_INPUT_VOWEL,
    CAN_MODIFY_VOWEL,
    CAN_INPUT_DIACRITIC,
    CAN_MODIFY_DIACRITIC,
    IS_PROCEEDING_LIGATURE // I could use CAN_MODIFY_VOWEL in place of this
};

struct AddendumContextState {
    bool hasAspirate;
    VowelStatus vowelStatus;
    std::unordered_map<KeySym, AddendumCharacter> modifierMappings;
};

static AddendumContextState addendumContextState = {
    false,
    CAN_INPUT_VOWEL,
    {}
};

static const std::unordered_map<KeySym, AddendumCharMapping> consonants1 = {
    {FcitxKey_k,{{0xd9a,0x001}, {}}},
    {FcitxKey_c,{{0xd9a,0x001}, {{FcitxKey_h,{0xda0,0x001}}}}},
    {FcitxKey_t,{{0xda7,0x001}, {{FcitxKey_h,{0xdad,0x001}}}}},
    {FcitxKey_p,{{0xdb4,0x001}, {}}},
    {FcitxKey_g,{{0xd9c,0x001}, {}}},
    {FcitxKey_j,{{0xd9c,0x001}, {}}},
    {FcitxKey_d,{{0xda9,0x001}, {{FcitxKey_h,{0xdaf,0x001}}}}},
    {FcitxKey_b,{{0xdb6,0x001}, {}}},

    {FcitxKey_n,{{0xdb1,0x000}, {}}},

    {FcitxKey_m,{{0xdb8,0x000}, {}}},
    {FcitxKey_y,{{0xdba,0x000}, {}}},
    {FcitxKey_r,{{0xdbb,0x000}, {}}},
    {FcitxKey_v,{{0xdc0,0x000}, {}}},

    {FcitxKey_s,{{0xdc3,0x000}, {{FcitxKey_h,{0xdc2,0x000}}}}},
    {FcitxKey_S,{{0xdc3,0x000}, {{FcitxKey_h,{0xdc1,0x000}}}}},
    {FcitxKey_h,{{0xdc4,0x000}, {}}},{FcitxKey_H,{{0xdc4,0x000}, {}}},
    {FcitxKey_f,{{0xdc6,0x000}, {}}},

};

static const std::unordered_map<KeySym, AddendumVowelMapping> vowels1 = {
    {FcitxKey_a,{0xd85, 0x002, {{FcitxKey_a, {0xd86, 0xdcf}}}}},
    {FcitxKey_A,{0xd87, 0xdd0, {{FcitxKey_a, {0xd88, 0xdd1}}}}},
    {FcitxKey_i,{0xd89, 0xdd2, {{FcitxKey_i, {0xd8a, 0xdd3}}}}},
    {FcitxKey_u,{0xd8b, 0xdd2, {{FcitxKey_u, {0xd8c, 0xdd3}}}}},
    {FcitxKey_e,{0xd91, 0xdd2, {{FcitxKey_e, {0xd92, 0xdd3}}}}},


};

template <typename T, size_t n>
std::unordered_map<KeySym, T> fillKeyMap(const std::array<T, n> &data) {
    std::unordered_map<KeySym, T> result;
    std::transform(
        data.begin(), data.end(), std::inserter(result, result.end()),
        [](const T &item) { return std::make_pair(item.key, item); });
    return result;
}

template <typename Map, typename Key>
const typename Map::mapped_type *findValue(const Map &map, const Key &key) {
    if (auto iter = map.find(key); iter != map.end()) {
        return &iter->second;
    }
    return nullptr;
}

class SayuraState : public InputContextProperty {
public:
    SayuraState(InputContext &ic) : ic_(&ic) {}

    ~SayuraState() {}

    void commitPreedit() {
        std::string str = bufferToUTF8();
        ic_->commitString(str);
        buffer_.clear();
    }

    void reset() {
        buffer_.clear();
        addendumContextState = {
            false,
            CAN_INPUT_VOWEL,
            {}
        };
        updateUI();
    }

    void updateUI() {
        auto &inputPanel = ic_->inputPanel();
        inputPanel.reset();
        auto str = bufferToUTF8();
        if (!str.empty()) {
            Text preedit(str, TextFormatFlag::HighLight);
            preedit.setCursor(str.size());
            if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) {
                inputPanel.setClientPreedit(preedit);
            } else {
                inputPanel.setPreedit(preedit);
            }
        }
        ic_->updatePreedit();
        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
    }

    bool backspace() {
        if (buffer_.empty()) {
            return false;
        }
        buffer_.pop_back();
        return true;
    }

    void handleAddConsonant(const KeySym key_sym, const AddendumCharMapping &addendum) {
        const AddendumCharacter addendumCons = addendum.character;
        const VowelStatus vowel_status = addendumContextState.vowelStatus;

        if(buffer_.empty() || vowel_status == CAN_INPUT_VOWEL || vowel_status == CAN_MODIFY_VOWEL) {
            if(vowel_status == CAN_MODIFY_VOWEL) {
                commitPreedit();
                addendumContextState.modifierMappings.clear();
            }
            buffer_.push_back(addendumCons.baseChar);
            buffer_.push_back(0xdca);

            addendumContextState.vowelStatus = CAN_INPUT_DIACRITIC;
            addendumContextState.hasAspirate = addendumCons.secondaryChar == 0x001;
            addendumContextState.modifierMappings.merge(
                static_cast<
                    std::unordered_map<_FcitxKeySym, AddendumCharacter>>(
                    addendum.leadingModCharMappings)
            );

            return;
        }

        if(!addendumContextState.modifierMappings.empty()) {
            auto find_value = findValue(addendumContextState.modifierMappings, key_sym);

            if (find_value != nullptr) {
                buffer_.pop_back(); buffer_.pop_back();
                buffer_.push_back(find_value->baseChar);
                buffer_.push_back(0xdca);
                addendumContextState.hasAspirate = (find_value->secondaryChar == 0x001);
                addendumContextState.modifierMappings.clear();
                // it shouldn't be possible to reach here with anything else than CAN_INPUT_DIACRITIC
                return;
            }
        }

        if(key_sym == FcitxKey_H && addendumContextState.hasAspirate) { // I just need to remember to remove hasApirate value if I can input a vowel
            buffer_.pop_back(); buffer_.pop_back();
            buffer_.push_back(buffer_.front()+1);
            buffer_.push_back(0xdca);

            addendumContextState.modifierMappings.clear();
            addendumContextState.hasAspirate = false;
            // if you can make the letter into an aspirate, the vowel status MUST already be CAN_INPUT_DIACRITIC
            return;
        }

        // Abstract this with another hashmap to make this work for any indic lang
        if(vowel_status == CAN_INPUT_DIACRITIC) {
            if(key_sym == FcitxKey_r || key_sym == FcitxKey_R || key_sym == FcitxKey_y || key_sym == FcitxKey_Y) {
                buffer_.push_back(0x200d);
                buffer_.push_back(addendumCons.baseChar);
                buffer_.push_back(0xdca);

                addendumContextState.modifierMappings.clear();
                addendumContextState.hasAspirate = false;

                return;
            }
        }


        if(vowel_status == CAN_INPUT_DIACRITIC || vowel_status == CAN_MODIFY_DIACRITIC) {
            commitPreedit();
            buffer_.push_back(addendumCons.baseChar);
            buffer_.push_back(0xdca);

            addendumContextState.vowelStatus = CAN_INPUT_DIACRITIC;
            addendumContextState.modifierMappings.clear();
            addendumContextState.modifierMappings.merge(
                static_cast<
                    std::unordered_map<_FcitxKeySym, AddendumCharacter>>(
                    addendum.leadingModCharMappings));
            return;
        }
    }

    void handleAdVowel(const KeySym key_sym, const AddendumVowelMapping &addendum) {
        const VowelStatus vowel_status = addendumContextState.vowelStatus;

        if(buffer_.empty() || vowel_status == CAN_INPUT_VOWEL) {
            const uint32_t addendumVow = addendum.baseChar;
            buffer_.push_back(addendumVow);

            addendumContextState.vowelStatus = CAN_MODIFY_VOWEL;
            addendumContextState.hasAspirate = false;
            addendumContextState.modifierMappings.clear();
            addendumContextState.modifierMappings.merge(
                static_cast<
                    std::unordered_map<_FcitxKeySym, AddendumCharacter>>(
                    addendum.leadingModCharMappings)
            );

            return;
        }

        if(vowel_status == CAN_MODIFY_VOWEL) {
            if(!addendumContextState.modifierMappings.empty()) {
                const AddendumCharacter *find_value = findValue(addendumContextState.modifierMappings, key_sym);

                if (find_value != nullptr) {
                    buffer_.pop_back();
                    buffer_.push_back(find_value->baseChar);
                    addendumContextState.hasAspirate = false;
                    addendumContextState.modifierMappings.clear();
                    commitPreedit();
                    return;
                } else {
                    // Mayhaps just enter a new raw vowel??
                    return;
                }
            }
        }

        if(vowel_status == CAN_INPUT_DIACRITIC) {
            const uint32_t addendumDia = addendum.diacritic;

            buffer_.pop_back();

            if(addendumDia != 0x002) {
                buffer_.push_back(addendumDia);
            }

            addendumContextState.vowelStatus = CAN_MODIFY_DIACRITIC;
            addendumContextState.hasAspirate = false;
            addendumContextState.modifierMappings.clear();
            addendumContextState.modifierMappings.merge(
                static_cast<
                    std::unordered_map<_FcitxKeySym, AddendumCharacter>>(
                    addendum.leadingModCharMappings)
            );

            return;
        }

        if(vowel_status == CAN_MODIFY_DIACRITIC) {
            if(!addendumContextState.modifierMappings.empty()) {
                const AddendumCharacter *find_value = findValue(addendumContextState.modifierMappings, key_sym);

                if (find_value != nullptr) {
                    if(buffer_.front() == 0xdca) {
                        buffer_.pop_back();
                    }
                    buffer_.push_back(find_value->secondaryChar);

                    addendumContextState.hasAspirate = false;
                    addendumContextState.modifierMappings.clear();
                    commitPreedit();
                    return;
                } else {
                    // Mayhaps just enter a new raw vowel??
                    return;
                }
            }
        }
    }

private:
    std::string bufferToUTF8() {
        std::string str;
        for (auto c : buffer_) {
            str.append(utf8::UCS4ToUTF8(c));
        }
        return str;
    }

    InputContext *ic_;
    std::vector<uint32_t> buffer_;
    //bool hasAspirate = false;
    //VowelStatus vowelStatus = CAN_INPUT_VOWEL;
    //std::unordered_map<KeySym, AddendumConsonant> modifierMappings = std::unordered_map<KeySym, AddendumConsonant>{};
    //fcitx::SayuraEngine * parent_;
};

SayuraEngine::SayuraEngine(Instance *instance)
    : instance_(instance),
      factory_([](InputContext &ic) { return new SayuraState(ic); }) {
    instance->inputContextManager().registerProperty("sayuraState", &factory_);
}

SayuraEngine::~SayuraEngine() {}

void SayuraEngine::activate(const InputMethodEntry &, InputContextEvent &) {}

void SayuraEngine::deactivate(const InputMethodEntry &,
                              InputContextEvent &event) {
    auto state = event.inputContext()->propertyFor(&factory_);
    state->commitPreedit();
    state->updateUI();
}

void SayuraEngine::keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) {
    auto key = keyEvent.key();
    auto ic = keyEvent.inputContext();
    auto state = ic->propertyFor(&factory_);
    if (keyEvent.isRelease()) {
        return;
    }


    //state->commitPreedit()
    if (key.check(FcitxKey_Escape)) {
        reset(entry, keyEvent);
        return;
    }
    if (key.check(FcitxKey_BackSpace)) {
        if (state->backspace()) {
            state->updateUI();
            keyEvent.filterAndAccept();
        }
        return;
    }

    if (key.check(FcitxKey_space)) {
        state->commitPreedit();
        state->updateUI();
        return;
    }

    if (key.states() != KeyState::NoState) {
        return;
    }
    // auto consonant = findConsonantByKey(key.sym());
    auto consonant = findValue(consonants1, key.sym());
    if (consonant != nullptr) {

        //state->handleConsonant(*consonant);
        state->handleAddConsonant(key.sym(), *consonant);
        state->updateUI();
        keyEvent.filterAndAccept();
        return;
    }

    auto vowel = findValue(vowels1, key.sym());
    if (vowel) {
        state->handleAdVowel(key.sym(), *vowel);
        //state->handleVowel(*vowel);
        state->updateUI();
        keyEvent.filterAndAccept();
        return;
    }

    if (key.sym() == FcitxKey_Shift_L || key.sym() == FcitxKey_Shift_R) {
        return;
    }

    state->commitPreedit();
    state->updateUI();
}

void SayuraEngine::reset(const InputMethodEntry &, InputContextEvent &event) {
    auto state = event.inputContext()->propertyFor(&factory_);
    state->reset();
}

} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::SayuraFactory);
