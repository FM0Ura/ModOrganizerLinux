#include "nexuspagepanel.h"
#include "bbcode.h"
#include "nexusimagecache.h"
#include "nexusinterface.h"
#include "nexuspage.h"
#include "nxmaccessmanager.h"
#include <utility.h>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QShowEvent>
#include <QUrl>
#include <QWebEngineSettings>
#include <QWebEngineView>

using namespace MOBase;

namespace
{
QString headerHtml(const ModInfo& mod)
{
  return QString("<h2>%1</h2><p>%2 · %3</p>")
      .arg(mod.name().toHtmlEscaped(), mod.author().toHtmlEscaped(),
           mod.version().canonicalString().toHtmlEscaped());
}

NXMAccessManager* accessManager()
{
  return NexusInterface::instance().getAccessManager();
}
}  // namespace

bool NexusPanelWebpage::acceptNavigationRequest(const QUrl& url,
                                                QWebEnginePage::NavigationType type,
                                                bool isMainFrame)
{
  if (type == QWebEnginePage::NavigationTypeLinkClicked) {
    emit linkClicked(url);
    return false;
  }

  // setHtml() loads its content as a typed navigation of the main frame
  return isMainFrame && type == QWebEnginePage::NavigationTypeTyped;
}

NexusPagePanel::NexusPagePanel(NexusImageCache& cache, QWidget* parent)
    : QWidget(parent), m_Cache(cache), m_View(nullptr), m_WaitingForLogin(false),
      m_LoadingFromNexus(false)
{
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  m_ViewLayout = new QVBoxLayout();
  m_ViewLayout->setContentsMargins(0, 0, 0, 0);
  layout->addLayout(m_ViewLayout, 1);

  auto* row     = new QHBoxLayout();
  m_Refresh     = new QPushButton(tr("Refresh"), this);
  m_OpenOnNexus = new QPushButton(tr("Open on Nexus"), this);
  m_Status      = new QLabel(this);
  m_Status->setWordWrap(true);
  row->addWidget(m_Refresh);
  row->addWidget(m_OpenOnNexus);
  row->addWidget(m_Status, 1);
  layout->addLayout(row);

  m_Timeout.setSingleShot(true);
  m_Timeout.setInterval(30000);

  connect(&m_Timeout, &QTimer::timeout, this, &NexusPagePanel::onTimeout);
  connect(m_Refresh, &QPushButton::clicked, this, &NexusPagePanel::onRefresh);
  connect(m_OpenOnNexus, &QPushButton::clicked, this, &NexusPagePanel::onOpenOnNexus);
  connect(&m_Cache, &NexusImageCache::imageReady, this,
          [this](const QString& gameName, int modId, const QString&) {
            onImageReady(gameName, modId);
          });
  connect(NexusInterface::instance().getAccessManager(),
          &NXMAccessManager::credentialsReceived, this,
          &NexusPagePanel::onCredentialsReceived);

  showEmpty();
}

void NexusPagePanel::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);
  ensureView();
}

void NexusPagePanel::ensureView()
{
  if (m_View) {
    return;
  }

  m_View     = new QWebEngineView(this);
  auto* page = new NexusPanelWebpage(m_View);
  m_View->setPage(page);
  // the description is third-party HTML loaded with a file:// base URL
  page->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, false);
  page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls,
                                 true);
  connect(page, &NexusPanelWebpage::linkClicked, this, [](const QUrl& url) {
    shell::Open(url);
  });
  m_ViewLayout->addWidget(m_View);

  if (!m_Html.isEmpty()) {
    m_View->setHtml(m_Html, baseUrl());
  }
}

QUrl NexusPagePanel::baseUrl() const
{
  return QUrl::fromLocalFile(QDir(m_Cache.cacheDir()).absolutePath() + "/");
}

void NexusPagePanel::showHtml(const QString& html)
{
  m_Html = html;
  if (m_View) {
    m_View->setHtml(m_Html, baseUrl());
  }
}

void NexusPagePanel::showEmpty()
{
  showHtml(nexuspage::pageHtml(
      QString(), QString(),
      tr("Click the document icon next to a mod to see its Nexus page here.")
          .toHtmlEscaped()));
  m_Status->clear();
  m_Refresh->setEnabled(false);
  m_OpenOnNexus->setEnabled(false);
}

void NexusPagePanel::disconnectMod()
{
  for (const auto& c : m_ModConnections) {
    QObject::disconnect(c);
  }
  m_ModConnections.clear();
}

void NexusPagePanel::clear()
{
  disconnectMod();
  m_Timeout.stop();
  m_WaitingForLogin  = false;
  m_LoadingFromNexus = false;
  const bool hadMod  = (m_Mod != nullptr);
  m_Mod.reset();
  showEmpty();
  if (hadMod) {
    emit modChanged(nullptr);
  }
}

void NexusPagePanel::rebindMod(ModInfo::Ptr mod)
{
  if (!mod || !m_Mod || mod == m_Mod) {
    return;
  }

  disconnectMod();
  m_Mod = mod;

  m_ModConnections.append(connect(mod.get(), &ModInfo::modDetailsUpdated, this,
                                  &NexusPagePanel::onModDetailsUpdated));
  m_ModConnections.append(connect(mod.get(), &ModInfo::nexusRequestFailed, this,
                                  &NexusPagePanel::onRequestFailed));

  if (m_LoadingFromNexus && accessManager()->isValidated()) {
    // the reply would go to the old object: ask again, keeping the running timeout
    mod->requestNexusInfo(nexuspage::panelRequestTag());
  }

  emit modChanged(mod);
}

void NexusPagePanel::setMod(ModInfo::Ptr mod)
{
  if (!mod) {
    clear();
    return;
  }

  disconnectMod();
  m_Timeout.stop();
  m_WaitingForLogin  = false;
  m_LoadingFromNexus = false;
  m_Mod              = mod;

  m_ModConnections.append(connect(mod.get(), &ModInfo::modDetailsUpdated, this,
                                  &NexusPagePanel::onModDetailsUpdated));
  m_ModConnections.append(connect(mod.get(), &ModInfo::nexusRequestFailed, this,
                                  &NexusPagePanel::onRequestFailed));

  m_OpenOnNexus->setEnabled(true);
  m_Refresh->setEnabled(true);
  showNormalStatus();

  if (!accessManager()->isValidated()) {
    // the saved content stays visible; the login message is only for an empty page
    m_WaitingForLogin = true;
    m_Refresh->setEnabled(false);
    showLoginStatus();
    reloadContent();
  } else if (mod->getNexusDescription().isEmpty() || mod->needsDescriptionUpdate()) {
    requestInfo();
  } else {
    reloadContent();
  }

  // the picture comes from the CDN, which needs no login
  fetchImage();

  emit modChanged(mod);
}

void NexusPagePanel::fetchImage()
{
  if (!m_Mod) {
    return;
  }
  const QString url = m_Mod->getNexusPictureUrl();
  if (!url.isEmpty()) {
    m_Cache.fetch(m_Mod->gameName(), m_Mod->nexusId(), url);
  }
}

void NexusPagePanel::reloadContent()
{
  if (!m_Mod) {
    return;
  }

  QString body;
  if (m_Mod->getNexusDescription().isEmpty() && m_WaitingForLogin) {
    body = tr("Log in to Nexus (Settings → Nexus) to see this mod's page.")
               .toHtmlEscaped();
  } else if (m_Mod->getNexusDescription().isEmpty() && m_LoadingFromNexus) {
    body = tr("Loading from Nexus…").toHtmlEscaped();
  } else {
    body = BBCode::convertToHTML(m_Mod->getNexusDescription());
  }

  showHtml(nexuspage::pageHtml(pictureFileUrl(), headerHtml(*m_Mod), body));
}

QString NexusPagePanel::pictureFileUrl() const
{
  const QString file = m_Cache.cachedFile(m_Mod->gameName(), m_Mod->nexusId(),
                                          m_Mod->getNexusPictureUrl());
  if (file.isEmpty()) {
    return {};
  }

  // the time of the file tells the page apart from one showing an older download
  QUrl url = QUrl::fromLocalFile(file);
  url.setQuery(
      "t=" + QString::number(QFileInfo(file).lastModified().toMSecsSinceEpoch()));
  return url.toString();
}

void NexusPagePanel::showLoginStatus()
{
  const QString hint = tr("Log in to Nexus (Settings → Nexus) to update this page.");
  if (m_Mod && !m_Mod->getNexusDescription().isEmpty()) {
    m_Status->setText(m_Status->text() + " · " + hint);
  } else {
    m_Status->setText(hint);
  }
}

void NexusPagePanel::requestInfo()
{
  if (!m_Mod) {
    return;
  }
  m_LoadingFromNexus = true;
  setBusy(true);
  m_Mod->requestNexusInfo(nexuspage::panelRequestTag());
  m_Timeout.start();
  reloadContent();
}

void NexusPagePanel::setBusy(bool busy)
{
  m_Refresh->setEnabled(!busy && m_Mod != nullptr);
  if (busy) {
    m_Status->setText(tr("Updating from Nexus…"));
  }
}

void NexusPagePanel::showNormalStatus()
{
  if (!m_Mod) {
    m_Status->clear();
    return;
  }
  m_Status->setText(nexuspage::statusText(m_Mod->getNexusLastModified(),
                                          m_Mod->getLastNexusQuery(),
                                          QDateTime::currentDateTime()));
}

void NexusPagePanel::onModDetailsUpdated(bool success)
{
  if (!success || !m_Mod) {
    return;
  }
  m_Timeout.stop();
  m_LoadingFromNexus = false;
  m_Refresh->setEnabled(true);
  showNormalStatus();
  reloadContent();
  fetchImage();
}

void NexusPagePanel::onRequestFailed(const QString& message)
{
  m_Timeout.stop();
  m_LoadingFromNexus = false;
  m_Refresh->setEnabled(m_Mod != nullptr);
  m_Status->setText(tr("Couldn't update: ") + message);
  reloadContent();
}

void NexusPagePanel::onTimeout()
{
  m_LoadingFromNexus = false;
  m_Refresh->setEnabled(m_Mod != nullptr);
  m_Status->setText(tr("Nexus didn't respond"));
  reloadContent();
}

void NexusPagePanel::onRefresh()
{
  if (!m_Mod || !accessManager()->isValidated()) {
    return;
  }
  requestInfo();
}

void NexusPagePanel::onOpenOnNexus()
{
  if (!m_Mod) {
    return;
  }
  shell::Open(QUrl(
      NexusInterface::instance().getModURL(m_Mod->nexusId(), m_Mod->gameName())));
}

void NexusPagePanel::onCredentialsReceived()
{
  if (m_WaitingForLogin && m_Mod) {
    setMod(m_Mod);
  }
}

void NexusPagePanel::onImageReady(const QString& gameName, int modId)
{
  if (!m_Mod || m_Mod->gameName() != gameName || m_Mod->nexusId() != modId) {
    return;
  }

  // a picture already on the page (cached, emitted at once by fetch()) needs no
  // reload, which would also lose the scroll position
  const QString url = pictureFileUrl();
  if (!url.isEmpty() && m_Html.contains("<img src=\"" + url + "\"")) {
    return;
  }

  reloadContent();
}
