#include <Geode/Geode.hpp>

#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>

#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

#include <chrono>
#include <sstream>

using namespace geode::prelude;

// Shared message checker logic
class MessageCheckerCommon {
protected:
    inline static std::chrono::steady_clock::time_point nextCheck = std::chrono::steady_clock::now();
    inline static bool bootChecked = false;

    static std::vector<std::string> split(const std::string& str, char delim) {
        std::vector<std::string> elems;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, delim)) {
            elems.push_back(item);
        }
        return elems;
    }

    static std::string cleanMessage(const std::string& msg) {
        auto parts = split(msg, ':');
        std::string cleaned;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (parts[i] == "7" || parts[i] == "8") {
                ++i;
                continue;
            }
            if (!cleaned.empty()) cleaned += ":";
            cleaned += parts[i];
        }
        return cleaned;
    }

    void showNotification(const std::string& title) {
        AchievementNotifier::sharedState()->notifyAchievement(
            title.c_str(), "", "GJ_messageIcon_001.png", false
        );
        log::info("Notified user with title: {}", title);
    }

    void checkMessages() {
        log::info("Checking for new messages");

        auto* acc = GJAccountManager::sharedState();
        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
            log::info("Not logged in, skipping check");
            return;
        }

        std::string postData = fmt::format("accountID={}&gjp2={}&secret=Wmfd2893gb7", acc->m_accountID, acc->m_GJP2);

        auto* req = new cocos2d::extension::CCHttpRequest();
        req->setUrl("https://www.boomlings.com/database/getGJMessages20.php");
        req->setRequestType(cocos2d::extension::CCHttpRequest::kHttpPost);
        req->setRequestData(postData.c_str(), postData.length());
        req->setTag("MessageCheck");

        req->setResponseCallback([this](auto, auto* resp) {
            log::info("Received response from server");
            if (!resp || !resp->isSucceed()) {
                log::info("Network request failed or was null");
                return;
            }

            std::string raw(resp->getResponseData()->begin(), resp->getResponseData()->end());
            log::info("Raw server response: {}", raw);

            if (raw.empty() || raw == "-1") {
                log::info("No messages found");
                return;
            }

            auto currentRaw = split(raw, '|');
            std::vector<std::string> currentClean;
            for (const auto& msg : currentRaw) {
                std::string cleaned = cleanMessage(msg);
                currentClean.push_back(cleaned);
                log::info("Cleaned message: {}", cleaned);
            }

            std::string saved = Mod::get()->getSavedValue<std::string>("last-messages", "");
            auto old = split(saved, '|');

            std::vector<std::string> newMessages;
            for (auto& clean : currentClean) {
                if (std::find(old.begin(), old.end(), clean) == old.end()) {
                    newMessages.push_back(clean);
                    log::info("New message detected: {}", clean);
                }
            }

            if (!newMessages.empty()) {
                if (newMessages.size() == 1) {
                    auto parts = split(newMessages[0], ':');
                    if (parts.size() > 9) {
                        std::string sender = parts[1];
                        std::string subjectBase64 = parts[9];

                        auto decoded = geode::utils::base64::decode(subjectBase64);
                        std::string subject = decoded.isOk()
                            ? std::string(decoded.unwrap().begin(), decoded.unwrap().end())
                            : "<Invalid base64>";

                        std::string notif = fmt::format("New Message!\nSent by: {}\n{}", sender, subject);
                        showNotification(notif);
                    } else {
                        showNotification("New Message!");
                    }
                } else {
                    showNotification(fmt::format("{} New Messages!", newMessages.size()));
                }
            } else {
                log::info("No new messages compared to saved list");
            }

            std::string save;
            for (size_t i = 0; i < currentClean.size(); ++i) {
                if (i > 0) save += "|";
                save += currentClean[i];
            }
            Mod::get()->setSavedValue("last-messages", save);
            log::info("Saved current message state to settings");
        });

        cocos2d::extension::CCHttpClient::getInstance()->send(req);
        req->release();
    }

    void onScheduledTick(float) {
        int interval = 300;
        try {
            interval = Mod::get()->getSettingValue<int>("check-interval");
            interval = std::clamp(interval, 60, 600);
            log::info("Using check interval from settings: {}", interval);
        } catch (...) {
            log::info("Failed to get check-interval setting, using default 300");
        }

        auto now = std::chrono::steady_clock::now();
        if (now >= nextCheck) {
            log::info("Interval elapsed, performing message check");
            checkMessages();
            nextCheck = now + std::chrono::seconds(interval);
        } else {
            auto remain = std::chrono::duration_cast<std::chrono::seconds>(nextCheck - now).count();
            log::info("Waiting for next check: {} seconds left", remain);
        }
    }

    template<typename T>
    void tryBoot(T* layer) {
        if (!bootChecked) {
            bootChecked = true;
            log::info("Boot sequence starting");
            layer->schedule(schedule_selector(MessageCheckerCommon::onScheduledTick), 1.0f);
            onScheduledTick(0);
        } else {
            log::info("Boot already done, rescheduling tick");
            layer->schedule(schedule_selector(MessageCheckerCommon::onScheduledTick), 1.0f);
        }
    }
};

// One $modify per layer

class $modify(MessageChecker_Menu, MenuLayer) : MessageCheckerCommon {
    bool init() {
        if (!MenuLayer::init()) return false;
        log::info("MenuLayer init");
        tryBoot(this);
        return true;
    }
};

class $modify(MessageChecker_Play, PlayLayer) : MessageCheckerCommon {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        log::info("PlayLayer init");
        tryBoot(this);
        return true;
    }
};

class $modify(MessageChecker_Editor, LevelEditorLayer) : MessageCheckerCommon {
    bool init(GJGameLevel* level, bool p1, bool p2) {
        if (!LevelEditorLayer::init(level, p1, p2)) return false;
        log::info("LevelEditorLayer init");
        tryBoot(this);
        return true;
    }
};
