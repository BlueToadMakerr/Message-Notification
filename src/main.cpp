#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/TextArea.hpp>
#include <Geode/loader/Log.hpp>

using namespace geode::prelude;

class $modify(MessageChecker, MenuLayer) { std::string m_lastMessageID; std::atomic<bool> m_checking = false;

void checkMessages() {
    if (m_checking) return;
    m_checking = true;

    std::string gjp2 = Mod::get()->getSavedValue<std::string>("gjp2");
    std::string accountID = Mod::get()->getSavedValue<std::string>("accID");

    if (gjp2.empty() || accountID.empty()) {
        log::warn("Missing GJP2 or accountID");
        m_checking = false;
        return;
    }

    web::AsyncWebRequest()
        .method("POST")
        .body("accountID=" + accountID + "&gjp2=" + gjp2 + "&page=0&total=0&getSent=0&secret=Wmfd2893gb7")
        .timeout(5.0)
        .url("https://www.boomlings.com/database/getGJMessages20.php")
        .fetch([this](web::WebResponse* response) {
            m_checking = false;
            if (!response->ok()) {
                log::warn("Message request failed");
                return;
            }
            std::string data = response->string();
            if (data.empty() || data == "-1") return;

            auto firstPipe = data.find('|');
            std::string firstMessage = data.substr(0, firstPipe);

            auto idIndex = firstMessage.find("~") + 1;
            std::string newMessageID = firstMessage.substr(idIndex, firstMessage.find("~", idIndex) - idIndex);
            if (newMessageID == m_lastMessageID) return;
            m_lastMessageID = newMessageID;

            auto senderIdx = firstMessage.find("~1~") + 3;
            auto senderEnd = firstMessage.find("~", senderIdx);
            std::string sender = firstMessage.substr(senderIdx, senderEnd - senderIdx);

            auto subjectIdx = firstMessage.find("~3~") + 3;
            auto subjectEnd = firstMessage.find("~", subjectIdx);
            std::string subject = firstMessage.substr(subjectIdx, subjectEnd - subjectIdx);
            subject = utils::string::urlDecode(subject);

            AchievementNotifier::get()->notify("New Message", fmt::format("From {}: {}", sender, subject), "GJ_messageIcon_001.png", [=](auto, auto) {
                auto mainLayer = this->getChildByID("main-layer");
                auto textNode = mainLayer->getChildByID("text-node");
                auto m_fields = AchievementNotifier::get();

                auto m_titleLabel = static_cast<CCLabelBMFont*>(textNode->getChildByID("title-label"));
                auto m_achievementDescription = static_cast<TextArea*>(textNode->getChildByID("description-label"));
                auto m_achievementSprite = static_cast<CCSprite*>(mainLayer->getChildByID("achievement-sprite"));

                std::string newDesc = fmt::format("From {}: {}", sender, subject);
                std::string iconString = "GJ_messageIcon_001.png";

                auto messageCountText = CCString::createWithFormat("%d New Message%s!", newMessages.size(), newMessages.size() == 1 ? "" : "s");
    auto newTitleLabel = CCLabelBMFont::create(messageCountText->getCString(), m_titleLabel->getFntFile());
                newTitleLabel->setPosition(m_titleLabel->getPosition());
                newTitleLabel->setScale(m_titleLabel->getScale());
                newTitleLabel->setContentSize(m_titleLabel->getContentSize());
                newTitleLabel->setAnchorPoint(m_titleLabel->getAnchorPoint());
                newTitleLabel->setOpacity(0);
                textNode->addChild(newTitleLabel);
                m_fields->m_newTitleLabel = newTitleLabel;

                auto newDescText = newMessages.size() == 1 ? fmt::format("From: {}

{}", newMessages[0].user, newMessages[0].subject) : newDesc; auto newAchievementDescription = TextArea::create( newDescText.c_str(), m_achievementDescription->m_fontFile.c_str(), m_achievementDescription->m_scale, m_achievementDescription->m_width, m_achievementDescription->m_anchorPoint, m_achievementDescription->m_height, m_achievementDescription->m_disableColor ); newAchievementDescription->setPosition(m_achievementDescription->getPosition()); newAchievementDescription->setScale(m_achievementDescription->getScale()); newAchievementDescription->setContentSize(m_achievementDescription->getContentSize()); newAchievementDescription->setAnchorPoint(m_achievementDescription->getAnchorPoint()); newAchievementDescription->setOpacity(0); textNode->addChild(newAchievementDescription); m_fields->m_newAchievementDescription = newAchievementDescription;

auto newAchievementSprite = CCSprite::createWithSpriteFrameName(iconString.c_str());
                newAchievementSprite->setPosition(m_achievementSprite->getPosition());
                newAchievementSprite->setScale(m_achievementSprite->getScale());
                newAchievementSprite->setAnchorPoint(m_achievementSprite->getAnchorPoint());
                newAchievementSprite->setZOrder(m_achievementSprite->getZOrder());
                newAchievementSprite->setOpacity(0);
                mainLayer->addChild(newAchievementSprite);
                m_fields->m_newAchievementSprite = newAchievementSprite;

                const float fadeTime = 0.4;
                const float delayTime = (m_fields->getQuestTime() - 0.8) / 2;

                m_titleLabel->runAction(CCSequence::createWithTwoActions(CCDelayTime::create(delayTime), CCFadeOut::create(fadeTime)));
                m_achievementSprite->runAction(CCSequence::createWithTwoActions(CCDelayTime::create(delayTime), CCFadeOut::create(fadeTime)));
                m_achievementDescription->runAction(CCSequence::createWithTwoActions(CCDelayTime::create(delayTime), CCFadeOut::create(fadeTime)));

                newTitleLabel->runAction(CCSequence::createWithTwoActions(CCDelayTime::create(delayTime), CCFadeIn::create(fadeTime)));
                newAchievementSprite->runAction(CCSequence::createWithTwoActions(CCDelayTime::create(delayTime), CCFadeIn::create(fadeTime)));
                newAchievementDescription->runAction(CCSequence::createWithTwoActions(CCDelayTime::create(delayTime), CCFadeIn::create(fadeTime)));
            });
        });
}

void update(float dt) override {
    MenuLayer::update(dt);
    static float timer = 0.f;
    timer += dt;
    if (timer >= 300.f) {
        timer = 0.f;
        checkMessages();
    }
}

bool init() {
    if (!MenuLayer::init()) return false;
    this->scheduleUpdate();
    return true;
}

};

