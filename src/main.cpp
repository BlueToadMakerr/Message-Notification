#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/cocos/extensions/network/HttpClient.h>

using namespace geode::prelude;

class $modify(MessageChecker, MenuLayer) {
    struct Fields {
        bool m_hasBooted = false;
    };

    static std::vector<std::string> split(const std::string& str, char delimiter) {
        std::vector<std::string> result;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, delimiter)) {
            result.push_back(item);
        }
        return result;
    }

    void showNotification(const std::string& title, const std::string& desc, const std::string& iconName = "GJ_messageIcon_001.png") {
        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto layer = CCLayer::create();
        auto bg = CCSprite::createWithSpriteFrameName("GJ_square01.png");
        bg->setScale(0.6f);
        bg->setPosition({ winSize.width / 2, winSize.height - 60 });
        layer->addChild(bg);

        auto titleLabel = CCLabelBMFont::create(title.c_str(), "goldFont.fnt");
        titleLabel->setPosition({ winSize.width / 2, winSize.height - 40 });
        titleLabel->setScale(0.5f);
        layer->addChild(titleLabel);

        auto descLabel = CCLabelBMFont::create(desc.c_str(), "chatFont.fnt", 220.0f, kCCTextAlignmentCenter);
        descLabel->setPosition({ winSize.width / 2, winSize.height - 65 });
        descLabel->setScale(0.4f);
        layer->addChild(descLabel);

        auto icon = CCSprite::createWithSpriteFrameName(iconName.c_str());
        if (icon) {
            icon->setScale(0.5f);
            icon->setPosition({ winSize.width / 2 - 90, winSize.height - 60 });
            layer->addChild(icon);
        }

        CCDirector::sharedDirector()->getRunningScene()->addChild(layer, 999);

        layer->runAction(CCSequence::create(
            CCDelayTime::create(4.0f),
            CCFadeOut::create(1.0f),
            CCCallFunc::create([layer]() {
                layer->removeFromParentAndCleanup(true);
            }),
            nullptr
        ));
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

        auto fields = split(messages.front(), ':');
        if (fields.size() < 3) {
            log::warn("[onMessageResponse] Unexpected message format.");
            return;
        }

        int latestID = std::stoi(fields[0]);
        std::string user = fields[1];
        std::string subject = fields[2];

        log::info("[onMessageResponse] Latest ID: {}, From: {}, Subject: {}", latestID, user, subject);

        int lastSeenID = Mod::get()->getSavedValue<int>("last-message-id", 0);
        log::debug("[onMessageResponse] Saved last ID: {}", lastSeenID);

        std::string desc = fmt::format("From: {}\n{}", user, subject);

        if (!m_fields->m_hasBooted) {
            log::info("[onMessageResponse] Showing message on boot.");
            showNotification("Latest Message", desc);
            Mod::get()->setSavedValue("last-message-id", latestID);
            m_fields->m_hasBooted = true;
            return;
        }

        if (latestID > lastSeenID) {
            log::info("[onMessageResponse] New message detected!");
            showNotification("New Message!", desc);
            Mod::get()->setSavedValue("last-message-id", latestID);
        } else {
            log::debug("[onMessageResponse] No new messages.");
        }
    }

    bool init() {
        if (!MenuLayer::init()) return false;

        log::info("[init] MessageChecker initialized.");
        this->schedule(schedule_selector(MessageChecker::checkMessages), 300.0f);
        this->checkMessages(0);
        return true;
    }
};