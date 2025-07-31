#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

using namespace geode::prelude;

class $modify(MessageChecker, MenuLayer) {
    static std::vector<std::string> split(const std::string& str, char delimiter) {
        std::vector<std::string> result;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, delimiter)) {
            result.push_back(item);
        }
        return result;
    }

    void onMessageResponse(cocos2d::CCObject* sender, void* data) {
        auto* resp = static_cast<cocos2d::extension::CCHttpResponse*>(data);
        if (!resp || !resp->isSucceed()) return;

        std::string response(resp->getResponseData()->begin(), resp->getResponseData()->end());
        if (response.empty() || response == "-1") return;

        std::vector<std::string> messages = split(response, '|');
        static size_t lastCount = 0;
        if (messages.size() <= lastCount) return;

        size_t newMessages = messages.size() - lastCount;
        lastCount = messages.size();

        std::string title = newMessages == 1 ? "New Message!" : fmt::format("{} New Messages!", newMessages);
        std::string desc = "Check your inbox!";
        std::string icon = "GJ_messageIcon_001.png";

        if (newMessages == 1) {
            auto fields = split(messages.back(), ':');
            if (fields.size() >= 3) {
                std::string user = fields[1];
                std::string subject = fields[2];
                desc = fmt::format("From: {}\n{}", user, subject);
            }
        }

        AchievementNotifier::sharedState()->notifyAchievement(title.c_str(), desc.c_str(), icon.c_str(), false);
    }

    void checkMessages(float) {
        auto* acc = GJAccountManager::sharedState();
        if (acc->m_accountID <= 0 || acc->m_GJP2.empty()) return;

        std::string postData = fmt::format(
            "accountID={}&gjp2={}&secret=Wmfd2893gb7",
            acc->m_accountID, acc->m_GJP2
        );

        auto* req = new cocos2d::extension::CCHttpRequest();
        req->setUrl("https://www.boomlings.com/database/getGJMessages20.php");
        req->setRequestType(cocos2d::extension::CCHttpRequest::kHttpPost);
        req->setRequestData(postData.c_str(), postData.size());
        req->setResponseCallback(this, cocos2d::extension::SEL_HttpResponse(&MessageChecker::onMessageResponse));
        cocos2d::extension::CCHttpClient::getInstance()->send(req);
        req->release();
    }

    bool init() {
        if (!MenuLayer::init()) return false;
        this->schedule(schedule_selector(MessageChecker::checkMessages), 300.0f); // Every 5 min
        return true;
    }
};