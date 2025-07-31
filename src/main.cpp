#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

using namespace geode::prelude;

class $modify(MessageChecker, MenuLayer) {
    struct Fields {
        bool m_hasBooted = false;
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

    static std::string serializeMessages(const std::vector<std::tuple<std::string, std::string, std::string>>& messages) {
        std::string out;
        for (const auto& [user, subject, content] : messages) {
            out += user + "%%" + subject + "%%" + content + "||";
        }
        return out;
    }

    static std::vector<std::tuple<std::string, std::string, std::string>> deserializeMessages(const std::string& data) {
        std::vector<std::tuple<std::string, std::string, std::string>> result;
        auto entries = split(data, '|');
        for (auto& e : entries) {
            if (e.empty()) continue;
            auto parts = split(e, '%');
            if (parts.size() < 6) continue; // each %% is split into 2
            result.emplace_back(parts[0], parts[2], parts[4]);
        }
        return result;
    }

    void showNotification(const std::string& title) {
        AchievementNotifier::sharedState()->notifyAchievement(
            title.c_str(),
            "",
            "GJ_messageIcon_001.png",
            false
        );
    }

    void checkMessages(float) {
        log::info("[checkMessages] Checking for new messages...");

        auto* acc = GJAccountManager::sharedState();
        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
            log::info("[checkMessages] Account not signed in or GJP2 missing.");
            return;
        }

        std::string postData = fmt::format("accountID={}&gjp2={}&secret=Wmfd2893gb7", acc->m_accountID, acc->m_GJP2);

        auto* req = new cocos2d::extension::CCHttpRequest();
        req->setUrl("https://www.boomlings.com/database/getGJMessages20.php");
        req->setRequestType(cocos2d::extension::CCHttpRequest::kHttpPost);
        req->setRequestData(postData.c_str(), postData.size());
        req->setTag("MessageCheck");

        req->setResponseCallback(this, SEL_HttpResponse(&MessageChecker::onMessageResponse));
        cocos2d::extension::CCHttpClient::getInstance()->send(req);
        req->release();
    }

    void onMessageResponse(cocos2d::extension::CCHttpClient*, cocos2d::extension::CCHttpResponse* resp) {
        log::info("[onMessageResponse] Response received");

        if (!resp || !resp->isSucceed()) {
            log::info("[onMessageResponse] Request failed or null response.");
            return;
        }

        std::string response(resp->getResponseData()->begin(), resp->getResponseData()->end());
        if (response.empty() || response == "-1") {
            log::info("[onMessageResponse] No messages found.");
            return;
        }

        auto rawMessages = split(response, '|');
        std::vector<std::tuple<std::string, std::string, std::string>> currentMessages;

        for (auto& raw : rawMessages) {
            auto parts = split(raw, ':');
            if (parts.size() < 4) continue;
            currentMessages.emplace_back(parts[1], parts[2], parts[3]);
        }

        if (currentMessages.empty()) {
            log::info("[onMessageResponse] No parsable messages.");
            return;
        }

        auto savedData = Mod::get()->getSavedValue<std::string>("last-messages", "");
        auto previousMessages = deserializeMessages(savedData);

        size_t newMsgCount = 0;
        for (auto& curMsg : currentMessages) {
            if (std::find(previousMessages.begin(), previousMessages.end(), curMsg) == previousMessages.end()) {
                ++newMsgCount;
            } else {
                break;
            }
        }

        if (!m_fields->m_hasBooted) {
            m_fields->m_hasBooted = true;
        } else if (newMsgCount > 0) {
            if (newMsgCount == 1) {
                auto& [user, subject, _] = currentMessages[0];
                std::string title = fmt::format("New Message!\nSent by: {}\n{}", user, subject);
                showNotification(title);
            } else {
                std::string title = fmt::format("{} New Messages!", newMsgCount);
                showNotification(title);
            }
        }

        std::string serialized = serializeMessages(currentMessages);
        Mod::get()->setSavedValue("last-messages", serialized);
    }

    bool init() {
        if (!MenuLayer::init()) return false;

        log::info("[init] MessageChecker initialized.");

        int interval = std::clamp(Mod::get()->getSavedValue<int>("check-interval", 300), 60, 600);
        this->schedule(schedule_selector(MessageChecker::checkMessages), static_cast<float>(interval));
        this->checkMessages(0);
        return true;
    }
};