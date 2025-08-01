#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/binding/MenuLayer.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/base64.hpp>

using namespace geode::prelude;

class MessageCheckerManager : public cocos2d::CCNode {
public:
    static MessageCheckerManager* get() {
        static MessageCheckerManager* instance = nullptr;
        if (!instance) {
            instance = new MessageCheckerManager();
            instance->init();
        }
        return instance;
    }

    bool init() override {
        if (!CCNode::init()) return false;
        this->retain(); // persist in memory
        log::info("boot: initializing persistent checker");
        checkMessages(); // first check
        scheduleCheck(); // recurring check
        return true;
    }

    void scheduleCheck() {
        float interval = getIntervalFromSettings();
        this->unscheduleAllSelectors();
        this->schedule(schedule_selector(MessageCheckerManager::scheduledCheck), interval);
        log::info("scheduler: scheduled check every {} seconds", interval);
    }

    void scheduledCheck(float) {
        log::info("interval: performing scheduled message check");
        checkMessages();
        scheduleCheck(); // refresh interval if changed
    }

    float getIntervalFromSettings() {
        int interval = 300;
        try {
            interval = std::clamp(Mod::get()->getSettingValue<int>("check-interval"), 60, 600);
        } catch (...) {
            log::info("interval setting missing or invalid, using default = 300");
        }
        return static_cast<float>(interval);
    }

    void checkMessages() {
        log::info("checking for new messages");

        auto* acc = GJAccountManager::sharedState();
        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
            log::info("not logged in, skipping check");
            return;
        }

        std::string postData = fmt::format("accountID={}&gjp2={}&secret=Wmfd2893gb7", acc->m_accountID, acc->m_GJP2);
        log::info("sending HTTP request with data: {}", postData);

        web::AsyncWebRequest()
            .post("https://www.boomlings.com/database/getGJMessages20.php")
            .body(postData)
            .into([=](std::string const& raw) {
                log::info("received response: {}", raw);

                if (raw.empty() || raw == "-1") {
                    log::info("no messages found");
                    return;
                }

                auto split = [](const std::string& str, char delim) {
                    std::vector<std::string> elems;
                    std::stringstream ss(str);
                    std::string item;
                    while (std::getline(ss, item, delim)) elems.push_back(item);
                    return elems;
                };

                auto clean = [&](const std::string& msg) {
                    auto parts = split(msg, ':');
                    std::string cleaned;
                    for (size_t i = 0; i < parts.size(); ++i) {
                        if (parts[i] == "7" || parts[i] == "8") { ++i; continue; }
                        if (!cleaned.empty()) cleaned += ":";
                        cleaned += parts[i];
                    }
                    return cleaned;
                };

                auto currentRaw = split(raw, '|');
                std::vector<std::string> currentClean;
                for (const auto& msg : currentRaw) {
                    currentClean.push_back(clean(msg));
                    log::info("cleaned message: {}", currentClean.back());
                }

                std::string saved = Mod::get()->getSavedValue<std::string>("last-messages", "");
                auto old = split(saved, '|');

                std::vector<std::string> newMessages;
                for (auto& clean : currentClean) {
                    if (std::find(old.begin(), old.end(), clean) == old.end()) {
                        newMessages.push_back(clean);
                        log::info("new message detected: {}", clean);
                    }
                }

                auto notify = [&](const std::string& title) {
                    AchievementNotifier::sharedState()->notifyAchievement(
                        title.c_str(),
                        "",
                        "GJ_messageIcon_001.png",
                        false
                    );
                    log::info("notified user with title: {}", title);
                };

                if (!newMessages.empty()) {
                    log::info("total new messages: {}", newMessages.size());
                    if (newMessages.size() == 1) {
                        auto parts = split(newMessages[0], ':');
                        if (parts.size() >= 10) {
                            std::string sender = parts[1];
                            std::string subjectBase64 = parts[9];
                            log::info("decoded base64 subject: {}", subjectBase64);
                            auto decoded = geode::utils::base64::decode(subjectBase64);
                            std::string subject;
                            if (decoded.isOk()) {
                                auto vec = decoded.unwrap();
                                subject = std::string(vec.begin(), vec.end());
                            } else {
                                subject = "<Invalid base64>";
                            }
                            notify(fmt::format("New Message!\nFrom: {}\n{}", sender, subject));
                        }
                    } else {
                        notify(fmt::format("{} New Messages!", newMessages.size()));
                    }
                }

                std::string save;
                for (size_t i = 0; i < currentClean.size(); ++i) {
                    if (i > 0) save += "|";
                    save += currentClean[i];
                }

                Mod::get()->setSavedValue("last-messages", save);
                log::info("saved current message state to settings");
            })
            .send();
    }
};

// Fixing $modify syntax (must name the class!)
$modify(MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        static bool booted = false;
        if (!booted) {
            booted = true;
            log::info("boot: starting message checker manager");
            CCDirector::sharedDirector()->getRunningScene()->addChild(MessageCheckerManager::get());
        }

        return true;
    }
}
