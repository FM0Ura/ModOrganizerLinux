#ifndef NEXUSPAGEPANEL_H
#define NEXUSPAGEPANEL_H

#include "modinfo.h"
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

class NexusImageCache;
class QWebEngineView;

// The "Nexus" tab of the side panel: picture and description of the selected mod,
// shown from what is saved and refreshed from Nexus when needed
class NexusPagePanel : public QWidget
{
  Q_OBJECT

public:
  explicit NexusPagePanel(NexusImageCache& cache, QWidget* parent = nullptr);

  void setMod(ModInfo::Ptr mod);
  void clear();
  ModInfo::Ptr mod() const { return m_Mod; }

signals:
  // nullptr when cleared
  void modChanged(ModInfo::Ptr mod);

protected:
  void showEvent(QShowEvent* event) override;

private:
  void ensureView();
  QUrl baseUrl() const;
  void showHtml(const QString& html);
  void showEmpty();
  void reloadContent();
  void requestInfo();
  void fetchImage();
  void showNormalStatus();
  void setBusy(bool busy);
  void disconnectMod();

  void onModDetailsUpdated(bool success);
  void onRequestFailed(const QString& message);
  void onTimeout();
  void onRefresh();
  void onOpenOnNexus();
  void onCredentialsReceived();
  void onImageReady(const QString& gameName, int modId);

  NexusImageCache& m_Cache;
  ModInfo::Ptr m_Mod;
  QWebEngineView* m_View;
  QVBoxLayout* m_ViewLayout;
  QString m_Html;
  QPushButton* m_Refresh;
  QPushButton* m_OpenOnNexus;
  QLabel* m_Status;
  QTimer m_Timeout;
  bool m_WaitingForLogin;
  bool m_LoadingFromNexus;
  QList<QMetaObject::Connection> m_ModConnections;
};

#endif  // NEXUSPAGEPANEL_H
