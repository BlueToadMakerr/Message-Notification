#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/loader/SettingNode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>
#include <Geode/loader/SettingNode.hpp>

using namespace geode::prelude;

class $modify(MessageChecker, MenuLayer) { struct Fields { int m_lastMessageID = 0; };

static std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delimiter)) {
        result.push_back(item);
    }
    return result;
}

void showNotification(const std::string& title) {
    AchievementNotifier::sharedState()->notifyAchievement(
        title.c_str(),
        " ", // Empty description
        "GJ_messageIcon_001.png",
        false
    );
}

void checkMessages(float) {
    log::info("[checkMessages] Checking for new messages...");

    auto* acc = GJAccountManager::sharedState();
    if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) {
        log::warn("[checkMessages] Account not signed in or GJP2 missing.");
        return;
    }

    log::debug("[checkMessages] accountID: {}, GJP2 size: {}", acc->m_accountID, acc->m_GJP2.size());

    std::string postData = fmt::format(
        "accountID={}&gjp2={}&secret=Wmfd2893gb7",
        acc->m_accountID, acc->m_GJP2
    );

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
    log::debug("[onMessageResponse] Response received");

    if (!resp || !resp->isSucceed()) {
        log::warn("[onMessageResponse] Request failed or null response.");
        return;
    }

    std::string response(resp->getResponseData()->begin(), resp->getResponseData()->end());
    log::debug("[onMessageResponse] Response data: {}", response);

    if (response.empty() || response == "-1") {
        log::info("[onMessageResponse] No messages found.");
        return;
    }

    auto messages = split(response, '|');
    if (messages.empty()) {
        log::info("[onMessageResponse] Message list empty.");
        return;
    }

    int lastSeenID = Mod::get()->getSavedValue<int>("last-message-id", 0);
    int newCount = 0;
    int latestID = lastSeenID;

    for (auto& message : messages) {
        auto fields = split(message, ':');
        if (fields.size() < 3) continue;
        int msgID = std::stoi(fields[0]);
        if (msgID > lastSeenID) {
            newCount++;
            latestID = std::max(latestID, msgID);
        }
    }

    if (newCount > 0) {
        std::string title;
        if (newCount == 1) {
            auto fields = split(messages.front(), ':');
            title = fmt::format("New Message!\nSent by: {}\n{}", fields[1], fields[2]);
        } else {
            title = fmt::format("{} New Messages!", newCount);
        }

        log::info("[onMessageResponse] {} new message(s) detected.", newCount);
        showNotification(title);
        Mod::get()->setSavedValue("last-message-id", latestID);
    } else {
        log::debug("[onMessageResponse] No new messages.");
    }
}

bool init() {
    if (!MenuLayer::init()) return false;

    log::info("[init] MessageChecker initialized.");

    int interval = Mod::get()->getSettingValue<int>("check-interval");

    log::info("[init] Scheduling message check every {} seconds", interval);
    this->schedule(schedule_selector(MessageChecker::checkMessages), interval);
    this->checkMessages(0);

    return true;
}

};

