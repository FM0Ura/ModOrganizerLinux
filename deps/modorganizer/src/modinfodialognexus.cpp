#include "modinfodialognexus.h"
#include "bbcode.h"
#include "iplugingame.h"
#include "nexuspage.h"
#include "organizercore.h"
#include "settings.h"
#include "ui_modinfodialog.h"
#include <log.h>
#include <utility.h>
#include <versioninfo.h>

using namespace MOBase;

bool isValidModID(int id)
{
  return (id > 0);
}

NexusTab::NexusTab(ModInfoDialogTabContext cx)
    : ModInfoDialogTab(std::move(cx)), m_requestStarted(false), m_loading(false)
{
  ui->modID->setValidator(new QIntValidator(ui->modID));
  ui->endorse->setVisible(core().settings().nexus().endorsementIntegration());
  ui->track->setVisible(core().settings().nexus().trackedIntegration());

  connect(ui->modID, &QLineEdit::editingFinished, [&] {
    onModIDChanged();
  });
  connect(ui->sourceGame,
          static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [&] {
            onSourceGameChanged();
          });
  connect(ui->version, &QLineEdit::editingFinished, [&] {
    onVersionChanged();
  });
  connect(ui->category, &QLineEdit::editingFinished, [&] {
    onCategoryChanged();
  });

  connect(ui->refresh, &QPushButton::clicked, [&] {
    onRefreshBrowser();
  });
  connect(ui->visitNexus, &QPushButton::clicked, [&] {
    onVisitNexus();
  });
  connect(ui->endorse, &QPushButton::clicked, [&] {
    onEndorse();
  });
  connect(ui->track, &QPushButton::clicked, [&] {
    onTrack();
  });

  connect(ui->hasCustomURL, &QCheckBox::toggled, [&] {
    onCustomURLToggled();
  });
  connect(ui->customURL, &QLineEdit::editingFinished, [&] {
    onCustomURLChanged();
  });
  connect(ui->visitCustomURL, &QPushButton::clicked, [&] {
    onVisitCustomURL();
  });
}

NexusTab::~NexusTab()
{
  cleanup();
}

void NexusTab::cleanup()
{
  if (m_modConnection) {
    disconnect(m_modConnection);
    m_modConnection = {};
  }
}

void NexusTab::clear()
{
  ui->modID->clear();
  ui->sourceGame->clear();
  ui->version->clear();
  ui->category->clear();
  ui->browser->setPage(new NexusTabWebpage(ui->browser));
  ui->hasCustomURL->setChecked(false);
  ui->customURL->clear();
  setHasData(false);
}

void NexusTab::update()
{
  QScopedValueRollback loading(m_loading, true);

  clear();

  ui->modID->setText(QString("%1").arg(mod().nexusId()));

  QString gameName = mod().gameName();
  ui->sourceGame->addItem(core().managedGame()->gameName(),
                          core().managedGame()->gameShortName());

  if (core().managedGame()->validShortNames().size() == 0) {
    ui->sourceGame->setDisabled(true);
  } else {
    for (auto game : plugin().plugins<MOBase::IPluginGame>()) {
      for (QString gameName : core().managedGame()->validShortNames()) {
        if (game->gameShortName().compare(gameName, Qt::CaseInsensitive) == 0) {
          ui->sourceGame->addItem(game->gameName(), game->gameShortName());
          break;
        }
      }
    }
  }

  ui->sourceGame->setCurrentIndex(ui->sourceGame->findData(gameName));

  ui->category->setText(QString("%1").arg(mod().getNexusCategory()));

  auto* page = new NexusTabWebpage(ui->browser);
  ui->browser->setPage(page);

  connect(page, &NexusTabWebpage::linkClicked, [&](const QUrl& url) {
    shell::Open(url);
  });

  ui->endorse->setEnabled((mod().endorsedState() == EndorsedState::ENDORSED_FALSE) ||
                          (mod().endorsedState() == EndorsedState::ENDORSED_NEVER));

  setHasData(mod().nexusId() >= 0);
}

void NexusTab::firstActivation()
{
  updateWebpage();
}

void NexusTab::setMod(ModInfoPtr mod, MOShared::FilesOrigin* origin)
{
  cleanup();

  ModInfoDialogTab::setMod(mod, origin);

  m_modConnection = connect(mod.data(), &ModInfo::modDetailsUpdated, [&] {
    onModChanged();
  });
}

bool NexusTab::usesOriginFiles() const
{
  return false;
}

void NexusTab::updateVersionColor()
{
  if (mod().version() != mod().newestVersion()) {
    ui->version->setStyleSheet("color: red");
    ui->version->setToolTip(
        tr("Current Version: %1").arg(mod().newestVersion().canonicalString()));
  } else {
    ui->version->setStyleSheet("color: green");
    ui->version->setToolTip(tr("No update available"));
  }
}

void NexusTab::updateWebpage()
{
  const int modID = mod().nexusId();

  if (isValidModID(modID)) {
    const QString nexusLink =
        NexusInterface::instance().getModURL(modID, mod().gameName());

    ui->visitNexus->setToolTip(nexusLink);
    refreshData(modID);
  } else {
    onModChanged();
  }

  ui->version->setText(mod().version().displayString());
  ui->hasCustomURL->setChecked(mod().hasCustomURL());
  ui->customURL->setText(mod().url());
  ui->customURL->setEnabled(mod().hasCustomURL());
  ui->visitCustomURL->setEnabled(mod().hasCustomURL());
  ui->visitCustomURL->setToolTip(mod().parseCustomURL().toString());

  updateTracking();
}

void NexusTab::updateTracking()
{
  if (mod().trackedState() == TrackedState::TRACKED_TRUE) {
    ui->track->setChecked(true);
    ui->track->setText(tr("Tracked"));
  } else {
    ui->track->setChecked(false);
    ui->track->setText(tr("Untracked"));
  }
}

void NexusTab::refreshData(int modID)
{
  if (tryRefreshData(modID)) {
    m_requestStarted = true;
  } else {
    onModChanged();
  }
}

bool NexusTab::tryRefreshData(int modID)
{
  if (isValidModID(modID) && !m_requestStarted) {
    if (mod().updateNXMInfo()) {
      ui->browser->setHtml("");
      return true;
    }
  }

  return false;
}

void NexusTab::onModChanged()
{
  m_requestStarted = false;

  const QString nexusDescription = mod().getNexusDescription();

  QString bodyHtml;

  if (nexusDescription.isEmpty()) {
    bodyHtml = tr(R"(
      <div style="text-align: center;">
      <p>This mod does not have a valid Nexus ID. You can add a custom web
      page for it in the "Custom URL" box below.</p>
      </div>)");
  } else {
    bodyHtml = BBCode::convertToHTML(nexusDescription);
  }

  const QString descriptionAsHTML = nexuspage::descriptionDocument(bodyHtml);

  ui->browser->page()->setHtml(descriptionAsHTML);
  updateVersionColor();
  updateTracking();
}

void NexusTab::onModIDChanged()
{
  if (m_loading) {
    return;
  }

  const int oldID = mod().nexusId();
  const int newID = ui->modID->text().toInt();

  if (oldID != newID) {
    mod().setNexusID(newID);
    mod().setLastNexusQuery(QDateTime::fromSecsSinceEpoch(0));

    ui->browser->page()->setHtml("");

    if (isValidModID(newID)) {
      refreshData(newID);
    }
  }
}

void NexusTab::onSourceGameChanged()
{
  if (m_loading) {
    return;
  }

  for (auto game : plugin().plugins<MOBase::IPluginGame>()) {
    if (game->gameName() == ui->sourceGame->currentText()) {
      mod().setGameName(game->gameShortName());
      mod().setLastNexusQuery(QDateTime::fromSecsSinceEpoch(0));
      refreshData(mod().nexusId());
      return;
    }
  }
}

void NexusTab::onVersionChanged()
{
  if (m_loading) {
    return;
  }

  MOBase::VersionInfo version(ui->version->text());
  mod().setVersion(version);
  updateVersionColor();
}

void NexusTab::onCategoryChanged()
{
  if (m_loading) {
    return;
  }

  int category = ui->category->text().toInt();
  mod().setNexusCategory(category);
}

void NexusTab::onRefreshBrowser()
{
  const auto modID = mod().nexusId();

  if (isValidModID(modID)) {
    mod().setLastNexusQuery(QDateTime::fromSecsSinceEpoch(0));
    updateWebpage();
  } else {
    log::info("Mod has no valid Nexus ID, info can't be updated.");
  }
}

void NexusTab::onVisitNexus()
{
  const int modID = mod().nexusId();

  if (isValidModID(modID)) {
    const QString nexusLink =
        NexusInterface::instance().getModURL(modID, mod().gameName());

    shell::Open(QUrl(nexusLink));
  }
}

void NexusTab::onEndorse()
{
  // use modPtr() instead of mod() or this because the callback may be
  // executed after the dialog is closed
  core().loggedInAction(parentWidget(), [m = modPtr()] {
    m->endorse(true);
  });
}

void NexusTab::onTrack()
{
  // use modPtr() instead of mod() or this because the callback may be
  // executed after the dialog is closed
  core().loggedInAction(parentWidget(), [m = modPtr()] {
    if (m->trackedState() == TrackedState::TRACKED_TRUE) {
      m->track(false);
    } else {
      m->track(true);
    }
  });
}

void NexusTab::onCustomURLToggled()
{
  if (m_loading) {
    return;
  }

  mod().setHasCustomURL(ui->hasCustomURL->isChecked());
  ui->customURL->setEnabled(mod().hasCustomURL());
  ui->visitCustomURL->setEnabled(mod().hasCustomURL());
}

void NexusTab::onCustomURLChanged()
{
  if (m_loading) {
    return;
  }

  mod().setCustomURL(ui->customURL->text());
  ui->visitCustomURL->setToolTip(mod().parseCustomURL().toString());
}

void NexusTab::onVisitCustomURL()
{
  const QUrl url = mod().parseCustomURL();
  if (url.isValid()) {
    shell::Open(url);
  }
}
