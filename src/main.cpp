#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

using namespace geode::prelude;

class $modify(MessageChecker, MenuLayer) {
    struct Fields {
        inline static bool bootChecked = false;
    };

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
        std::vector<std::string> parts = split(msg, ':');
        std::string cleaned;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (parts[i] == "7" || parts[i] == "8") {
                ++i; // Skip tag and its value
                continue;
            }
            if (!cleaned.empty()) cleaned += ":";
            cleaned += parts[i];
        }
        return cleaned;
    }

    void showNotification(const std::string& title) {
        AchievementNotifier::sharedState()->notifyAchievement(
            title.c_str(),
            "",
            "GJ_messageIcon_001.png",
            false
        );
        log::info("notified user with title: {}", title);
    }

    void checkMessages() {
        log::info("checking: checking for new messages");

        auto* acc = GJAccountManager::sharedState();
        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
            log::info("checking: not logged in, skipping");
            return;
        }

        std::string postData = fmt::format("accountID={}&gjp2={}&secret=Wmfd2893gb7", acc->m_accountID, acc->m_GJP2);
        log::info("checking: sending HTTP request: {}", postData);

        auto* req = new cocos2d::extension::CCHttpRequest();
        req->setUrl("https://www.boomlings.com/database/getGJMessages20.php");
        req->setRequestType(cocos2d::extension::CCHttpRequest::kHttpPost);
        req->setRequestData(postData.c_str(), postData.length());
        req->setTag("MessageCheck");

        req->setResponseCallback(this, SEL_HttpResponse(&MessageChecker::onMessageResponse));
        cocos2d::extension::CCHttpClient::getInstance()->send(req);
        req->release();
    }

    void onMessageResponse(cocos2d::extension::CCHttpClient*, cocos2d::extension::CCHttpResponse* resp) {
        log::info("received: HTTP response from server");

        if (!resp || !resp->isSucceed()) {
            log::info("received: network failed or response was null");
            return;
        }

        std::string raw(resp->getResponseData()->begin(), resp->getResponseData()->end());
        log::info("received: raw response = {}", raw);

        if (raw.empty() || raw == "-1") {
            log::info("received: no messages found");
            return;
        }

        auto currentRaw = split(raw, '|');
        std::vector<std::string> currentClean;
        for (const auto& msg : currentRaw) {
            std::string cleaned = cleanMessage(msg);
            currentClean.push_back(cleaned);
            log::info("received: cleaned message = {}", cleaned);
        }

        std::string saved = Mod::get()->getSavedValue<std::string>("last-messages", "");
        auto old = split(saved, '|');

        std::vector<std::string> newMessages;
        for (auto& clean : currentClean) {
            if (std::find(old.begin(), old.end(), clean) == old.end()) {
                newMessages.push_back(clean);
                log::info("received: new message = {}", clean);
            }
        }

        if (!newMessages.empty()) {
            log::info("received: total new messages = {}", newMessages.size());

            if (newMessages.size() == 1) {
                auto parts = split(newMessages[0], ':');
                if (parts.size() >= 10) {
                    std::string sender = parts[1];
                    std::string subjectBase64 = parts[9];
                    log::info("received: base64 subject = {}", subjectBase64);

                    auto decoded = geode::utils::base64::decode(subjectBase64);
                    std::string subject;
                    if (decoded.isOk()) {
                        auto vec = decoded.unwrap();
                        subject = std::string(vec.begin(), vec.end());
                        log::info("received: decoded subject = {}", subject);
                    } else {
                        subject = "<Invalid base64>";
                        log::info("received: base64 decode failed");
                    }

                    std::string notif = fmt::format("New Message!\nSent by: {}\n{}", sender, subject);
                    showNotification(notif);
                }
            } else {
                showNotification(fmt::format("{} New Messages!", newMessages.size()));
            }
        } else {
            log::info("received: no new messages compared to saved");
        }

        std::string save;
        for (size_t i = 0; i < currentClean.size(); ++i) {
            if (i > 0) save += "|";
            save += currentClean[i];
        }

        Mod::get()->setSavedValue("last-messages", save);
        log::info("saved: updated current message state");
    }

    void runCheckLoop() {
        log::info("loop: running message check");

        checkMessages();

        int interval = 300;
        try {
            interval = std::clamp(Mod::get()->getSettingValue<int>("check-interval"), 60, 600);
            log::info("loop: using interval from settings = {}", interval);
        } catch (...) {
            log::info("loop: failed to get interval setting, default = {}", interval);
        }

        log::info("loop: scheduling next check in {} seconds", interval);
        auto delay = CCDelayTime::create(static_cast<float>(interval));
        auto call = CCCallFunc::create(this, callfunc_selector(MessageChecker::onInterval));
        auto seq = CCSequence::create(delay, call, nullptr);
        this->runAction(seq);
    }

    void onInterval() {
        log::info("interval: timer triggered");
        runCheckLoop(); // Re-run the loop, re-evaluates setting and re-checks
    }

    bool init() {
        if (!MenuLayer::init()) return false;

        log::info("init: MenuLayer loaded");

        if (!Fields::bootChecked) {
            Fields::bootChecked = true;
            log::info("boot: first time boot, starting check loop");
            runCheckLoop();
        } else {
            log::info("boot: already booted, skipping startup check");
        }

        return true;
    }
};
