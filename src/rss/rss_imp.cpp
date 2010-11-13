/*
 * Bittorrent Client using Qt4 and libtorrent.
 * Copyright (C) 2006  Christophe Dumez, Arnaud Demaiziere
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 *
 * Contact : chris@qbittorrent.org arnaud@qbittorrent.org
 */

#include <QDesktopServices>
#include <QInputDialog>
#include <QMenu>
#include <QStandardItemModel>
#include <QMessageBox>
#include <QString>
#include <QClipboard>
#include <QDragMoveEvent>

#include "rss_imp.h"
#include "feedlistwidget.h"
#include "qbtsession.h"
#include "cookiesdlg.h"
#include "preferences.h"
#include "rsssettingsdlg.h"
#include "rssmanager.h"
#include "rssfolder.h"
#include "rssarticle.h"
#include "rssfeed.h"
#include "automatedrssdownloader.h"

enum NewsCols { NEWS_ICON, NEWS_TITLE_COL, NEWS_URL_COL, NEWS_ID };

// display a right-click menu
void RSSImp::displayRSSListMenu(const QPoint& pos){
  if(!listStreams->indexAt(pos).isValid()) {
    // No item under the mouse, clear selection
    listStreams->clearSelection();
  }
  QMenu myRSSListMenu(this);
  QList<QTreeWidgetItem*> selectedItems = listStreams->selectedItems();
  if(selectedItems.size() > 0) {
    myRSSListMenu.addAction(actionUpdate);
    myRSSListMenu.addAction(actionMark_items_read);
    myRSSListMenu.addSeparator();
    if(selectedItems.size() == 1) {
      if(listStreams->getRSSItem(selectedItems.first()) != rssmanager) {
        myRSSListMenu.addAction(actionRename);
        myRSSListMenu.addAction(actionDelete);
        myRSSListMenu.addSeparator();
        if(listStreams->getItemType(selectedItems.first()) == RssFile::FOLDER) {
          myRSSListMenu.addAction(actionNew_folder);
        } else {
          myRSSListMenu.addAction(actionManage_cookies);
        }
      }
    }
    myRSSListMenu.addAction(actionNew_subscription);
    if(listStreams->getItemType(selectedItems.first()) == RssFile::FEED) {
      myRSSListMenu.addSeparator();
      myRSSListMenu.addAction(actionCopy_feed_URL);
    }
  }else{
    myRSSListMenu.addAction(actionNew_subscription);
    myRSSListMenu.addAction(actionNew_folder);
    myRSSListMenu.addSeparator();
    myRSSListMenu.addAction(actionUpdate_all_feeds);
  }
  myRSSListMenu.exec(QCursor::pos());
}

void RSSImp::displayItemsListMenu(const QPoint&){
  QMenu myItemListMenu(this);
  QList<QTreeWidgetItem*> selectedItems = listNews->selectedItems();
  if(selectedItems.size() > 0) {
    bool has_attachment = false;
    foreach(QTreeWidgetItem *item, selectedItems) {
      qDebug("text(3) URL: %s", qPrintable(item->text(NEWS_URL_COL)));
      qDebug("text(2) TITLE: %s", qPrintable(item->text(NEWS_TITLE_COL)));
      if(listStreams->getRSSItemFromUrl(item->text(NEWS_URL_COL))->getItem(item->text(NEWS_ID))->has_attachment()) {
        has_attachment = true;
        break;
      }
    }
    if(has_attachment)
      myItemListMenu.addAction(actionDownload_torrent);
    myItemListMenu.addAction(actionOpen_news_URL);
  }
  myItemListMenu.exec(QCursor::pos());
}

void RSSImp::on_actionManage_cookies_triggered() {
  Q_ASSERT(!listStreams->selectedItems().empty());
  // Get feed hostname
  QString feed_url = listStreams->getItemID(listStreams->selectedItems().first());
  QString feed_hostname = QUrl::fromEncoded(feed_url.toLocal8Bit()).host();
  qDebug("RSS Feed hostname is: %s", qPrintable(feed_hostname));
  Q_ASSERT(!feed_hostname.isEmpty());
  bool ok = false;
  QList<QByteArray> raw_cookies = CookiesDlg::askForCookies(this, Preferences::getHostNameCookies(feed_hostname), &ok);
  if(ok) {
    Preferences::setHostNameCookies(feed_hostname, raw_cookies);
  }
}

void RSSImp::askNewFolder() {
  QTreeWidgetItem *parent_item = 0;
  RssFolder *rss_parent;
  if(listStreams->selectedItems().size() > 0) {
    parent_item = listStreams->selectedItems().at(0);
    rss_parent = (RssFolder*)listStreams->getRSSItem(parent_item);
    Q_ASSERT(rss_parent->getType() == RssFile::FOLDER);
  } else {
    rss_parent = rssmanager;
  }
  bool ok;
  QString new_name = QInputDialog::getText(this, tr("Please choose a folder name"), tr("Folder name:"), QLineEdit::Normal, tr("New folder"), &ok);
  if(ok) {
    RssFolder* new_folder = rss_parent->addFolder(new_name);
    QTreeWidgetItem* folder_item;
    if(parent_item)
      folder_item = new QTreeWidgetItem(parent_item);
    else
      folder_item = new QTreeWidgetItem(listStreams);
    // Notify TreeWidget
    listStreams->itemAdded(folder_item, new_folder);
    // Set Text
    folder_item->setText(0, new_folder->getName() + QString::fromUtf8("  (0)"));
    folder_item->setData(0,Qt::DecorationRole, QVariant(QIcon(":/Icons/oxygen/folder.png")));
    // Expand parent folder to display new folder
    if(parent_item)
      parent_item->setExpanded(true);
    rssmanager->saveStreamList();
  }
}

void RSSImp::displayOverwriteError(QString filename) {
  QMessageBox::warning(this, tr("Overwrite attempt"),
                       tr("You cannot overwrite %1 item.", "You cannot overwrite myFolder item.").arg(filename),
                       QMessageBox::Ok);
}

// add a stream by a button
void RSSImp::on_newFeedButton_clicked() {
  // Determine parent folder for new feed
  QTreeWidgetItem *parent_item = 0;
  QList<QTreeWidgetItem *> selected_items = listStreams->selectedItems();
  if(!selected_items.empty()) {
    parent_item = selected_items.first();
    // Consider the case where the user clicked on Unread item
    if(parent_item == listStreams->getUnreadItem()) {
      parent_item = 0;
    } else {
      if(listStreams->getItemType(parent_item) != RssFile::FOLDER)
        parent_item = parent_item->parent();
    }
  }
  RssFolder *rss_parent;
  if(parent_item) {
    rss_parent = (RssFolder*)listStreams->getRSSItem(parent_item);
  } else {
    rss_parent = rssmanager;
  }
  // Ask for feed URL
  bool ok;
  QString clip_txt = qApp->clipboard()->text();
  QString default_url = "http://";
  if(clip_txt.startsWith("http://", Qt::CaseInsensitive) || clip_txt.startsWith("https://", Qt::CaseInsensitive) || clip_txt.startsWith("ftp://", Qt::CaseInsensitive)) {
    default_url = clip_txt;
  }
  QString newUrl = QInputDialog::getText(this, tr("Please type a rss stream url"), tr("Stream URL:"), QLineEdit::Normal, default_url, &ok);
  if(ok) {
    newUrl = newUrl.trimmed();
    if(!newUrl.isEmpty()){
      if(listStreams->hasFeed(newUrl)) {
        QMessageBox::warning(this, tr("qBittorrent"),
                             tr("This rss feed is already in the list."),
                             QMessageBox::Ok);
        return;
      }
      RssFeed *stream = rss_parent->addStream(newUrl);
      // Create TreeWidget item
      QTreeWidgetItem* item;
      if(parent_item)
        item = new QTreeWidgetItem(parent_item);
      else
        item = new QTreeWidgetItem(listStreams);
      // Notify TreeWidget
      listStreams->itemAdded(item, stream);
      // Set text
      item->setText(0, stream->getName() + QString::fromUtf8("  (0)"));
      item->setData(0,Qt::DecorationRole, QVariant(QIcon(":/Icons/loading.png")));
      stream->refresh();
      rssmanager->saveStreamList();
    }
  }
}

// delete a stream by a button
void RSSImp::deleteSelectedItems() {
  QList<QTreeWidgetItem*> selectedItems = listStreams->selectedItems();
  if(selectedItems.size() == 0) return;
  int ret;
  if(selectedItems.size() > 1)
    ret = QMessageBox::question(this, tr("Are you sure? -- qBittorrent"), tr("Are you sure you want to delete these elements from the list?"),
                                tr("&Yes"), tr("&No"),
                                QString(), 0, 1);
  else
    ret = QMessageBox::question(this, tr("Are you sure? -- qBittorrent"), tr("Are you sure you want to delete this element from the list?"),
                                tr("&Yes"), tr("&No"),
                                QString(), 0, 1);
  if(!ret) {
    foreach(QTreeWidgetItem *item, selectedItems){
      if(listStreams->currentFeed() == item){
        textBrowser->clear();
        previous_news = 0;
        listNews->clear();
      }
      RssFile *rss_item = listStreams->getRSSItem(item);
      // Notify TreeWidget
      listStreams->itemAboutToBeRemoved(item);
      // Actually delete the item
      rss_item->getParent()->removeFile(rss_item->getID());
      delete item;
    }
    rssmanager->saveStreamList();
    // Update Unread items
    updateItemInfos(listStreams->getUnreadItem());
  }
}

void RSSImp::loadFoldersOpenState() {
  QIniSettings settings("qBittorrent", "qBittorrent");
  settings.beginGroup("Rss");
  QStringList open_folders = settings.value("open_folders", QStringList()).toStringList();
  settings.endGroup();
  foreach(QString var_path, open_folders) {
    QStringList path = var_path.split("\\");
    QTreeWidgetItem *parent = 0;
    foreach(QString name, path) {
      int nbChildren = 0;
      if(parent)
        nbChildren = parent->childCount();
      else
        nbChildren = listStreams->topLevelItemCount();
      for(int i=0; i<nbChildren; ++i) {
        QTreeWidgetItem* child;
        if(parent)
          child = parent->child(i);
        else
          child = listStreams->topLevelItem(i);
        if(listStreams->getRSSItem(child)->getID() == name) {
          parent = child;
          parent->setExpanded(true);
          qDebug("expanding folder %s", qPrintable(name));
          break;
        }  
      }
    }
  }
}

void RSSImp::saveFoldersOpenState() {
  QStringList open_folders;
  QList<QTreeWidgetItem*> items = listStreams->getAllOpenFolders();
  foreach(QTreeWidgetItem* item, items) {
    QString path = listStreams->getItemPath(item).join("\\");
    qDebug("saving open folder: %s", qPrintable(path));
    open_folders << path;
  }
  QIniSettings settings("qBittorrent", "qBittorrent");
  settings.beginGroup("Rss");
  settings.setValue("open_folders", open_folders);
  settings.endGroup();
}

// refresh all streams by a button
void RSSImp::on_updateAllButton_clicked() {
  foreach(QTreeWidgetItem *item, listStreams->getAllFeedItems()) {
    item->setData(0,Qt::DecorationRole, QVariant(QIcon(":/Icons/loading.png")));
  }
  rssmanager->refreshAll();
}

void RSSImp::downloadTorrent() {
  QList<QTreeWidgetItem *> selected_items = listNews->selectedItems();
  foreach(const QTreeWidgetItem* item, selected_items) {
    RssArticle* article =  listStreams->getRSSItemFromUrl(item->text(NEWS_URL_COL))->getItem(item->text(NEWS_ID));
    if(article->has_attachment()) {
      QBtSession::instance()->downloadFromUrl(article->getTorrentUrl());
    } else {
      QBtSession::instance()->downloadFromUrl(article->getLink());
    }
  }
}

// open the url of the news in a browser
void RSSImp::openNewsUrl() {
  QList<QTreeWidgetItem *> selected_items = listNews->selectedItems();
  foreach(const QTreeWidgetItem* item, selected_items) {
    RssArticle* news =  listStreams->getRSSItemFromUrl(item->text(NEWS_URL_COL))->getItem(item->text(NEWS_ID));
    QString link = news->getLink();
    if(!link.isEmpty())
      QDesktopServices::openUrl(QUrl(link));
  }
}

//right-click on stream : give it an alias
void RSSImp::renameFiles() {
  QList<QTreeWidgetItem*> selectedItems = listStreams->selectedItems();
  Q_ASSERT(selectedItems.size() == 1);
  QTreeWidgetItem *item = selectedItems.at(0);
  RssFile *rss_item = listStreams->getRSSItem(item);
  bool ok;
  QString newName;
  do {
    newName = QInputDialog::getText(this, tr("Please choose a new name for this RSS feed"), tr("New feed name:"), QLineEdit::Normal, listStreams->getRSSItem(item)->getName(), &ok);
    // Check if name is already taken
    if(ok) {
      if(rss_item->getParent()->contains(newName)) {
        QMessageBox::warning(0, tr("Name already in use"), tr("This name is already used by another item, please choose another one."));
        ok = false;
      }
    } else {
      return;
    }
  }while(!ok);
  // Rename item
  rss_item->rename(newName);
  // Update TreeWidget
  updateItemInfos(item);
}

//right-click on stream : refresh it
void RSSImp::refreshSelectedItems() {
  QList<QTreeWidgetItem*> selectedItems = listStreams->selectedItems();
  foreach(QTreeWidgetItem* item, selectedItems){
    RssFile* file = listStreams->getRSSItem(item);
    // Update icons
    if(item == listStreams->getUnreadItem()) {
      foreach(QTreeWidgetItem *feed, listStreams->getAllFeedItems()) {
        feed->setData(0,Qt::DecorationRole, QVariant(QIcon(":/Icons/loading.png")));
      }
      file->refresh();
      break;
    } else {
      if(file->getType() == RssFile::FEED) {
        item->setData(0,Qt::DecorationRole, QVariant(QIcon(":/Icons/loading.png")));
      } else {
        // Update feeds in the folder
        foreach(QTreeWidgetItem *feed, listStreams->getAllFeedItems(item)) {
          feed->setData(0,Qt::DecorationRole, QVariant(QIcon(":/Icons/loading.png")));
        }
      }
    }
    // Actually refresh
    file->refresh();
  }
}

void RSSImp::copySelectedFeedsURL() {
  QStringList URLs;
  QList<QTreeWidgetItem*> selectedItems = listStreams->selectedItems();
  QTreeWidgetItem* item;
  foreach(item, selectedItems){
    if(listStreams->getItemType(item) == RssFile::FEED)
      URLs << listStreams->getItemID(item);
  }
  qApp->clipboard()->setText(URLs.join("\n"));
}

void RSSImp::on_markReadButton_clicked() {
  QList<QTreeWidgetItem*> selectedItems = listStreams->selectedItems();
  QTreeWidgetItem* item;
  foreach(item, selectedItems){
    RssFile *rss_item = listStreams->getRSSItem(item);
    rss_item->markAllAsRead();
    updateItemInfos(item);
  }
  if(selectedItems.size())
    refreshNewsList(listStreams->currentItem());
}

void RSSImp::fillFeedsList(QTreeWidgetItem *parent, RssFolder *rss_parent) {
  QList<RssFile*> children;
  if(parent) {
    children = rss_parent->getContent();
  } else {
    children = rssmanager->getContent();
  }
  foreach(RssFile* rss_child, children){
    QTreeWidgetItem* item;
    if(!parent)
      item = new QTreeWidgetItem(listStreams);
    else
      item = new QTreeWidgetItem(parent);
    item->setData(0, Qt::DisplayRole, QVariant(rss_child->getName()+ QString::fromUtf8("  (")+QString::number(rss_child->getNbUnRead(), 10)+QString(")")));
    // Notify TreeWidget of item addition
    listStreams->itemAdded(item, rss_child);
    // Set Icon
    if(rss_child->getType() == RssFile::FEED) {
      item->setData(0,Qt::DecorationRole, QVariant(QIcon(QString::fromUtf8(":/Icons/loading.png"))));
    } else {
      item->setData(0,Qt::DecorationRole, QVariant(QIcon(QString::fromUtf8(":/Icons/oxygen/folder.png"))));
      // Recurvive call to load sub folders/files
      fillFeedsList(item, (RssFolder*)rss_child);
    }
  }
}

// fills the newsList
void RSSImp::refreshNewsList(QTreeWidgetItem* item) {
  if(!item) {
    listNews->clear();
    return;
  }

  RssFile *rss_item = listStreams->getRSSItem(item);
  if(!rss_item) return;

  qDebug("Getting the list of news");
  QList<RssArticle*> news;
  if(rss_item == rssmanager)
    news = RssManager::sortNewsList(rss_item->getUnreadNewsList());
  else if(rss_item)
    news = RssManager::sortNewsList(rss_item->getNewsList());
  // Clear the list first
  textBrowser->clear();
  previous_news = 0;
  listNews->clear();
  qDebug("Got the list of news");
  foreach(RssArticle* article, news){
    QTreeWidgetItem* it = new QTreeWidgetItem(listNews);
    it->setText(NEWS_TITLE_COL, article->getTitle());
    it->setText(NEWS_URL_COL, article->getParent()->getUrl());
    it->setText(NEWS_ID, article->getId());
    if(article->isRead()){
      it->setData(NEWS_TITLE_COL, Qt::ForegroundRole, QVariant(QColor("grey")));
      it->setData(NEWS_ICON, Qt::DecorationRole, QVariant(QIcon(":/Icons/sphere.png")));
    }else{
      it->setData(NEWS_TITLE_COL, Qt::ForegroundRole, QVariant(QColor("blue")));
      it->setData(NEWS_ICON, Qt::DecorationRole, QVariant(QIcon(":/Icons/sphere2.png")));
    }
  }
  qDebug("Added all news to the GUI");
  qDebug("First news selected");
}

// display a news
void RSSImp::refreshTextBrowser() {
  QList<QTreeWidgetItem*> selection = listNews->selectedItems();
  if(selection.empty()) return;
  Q_ASSERT(selection.size() == 1);
  QTreeWidgetItem *item = selection.first();
  Q_ASSERT(item);
  if(item == previous_news) return;
  // Stop displaying previous news if necessary
  if(listStreams->currentFeed() == listStreams->getUnreadItem()) {
    if(previous_news) {
      disconnect(listNews, SIGNAL(itemSelectionChanged()), this, SLOT(refreshTextBrowser()));
      listNews->removeItemWidget(previous_news, 0);
      Q_ASSERT(previous_news);
      delete previous_news;
      connect(listNews, SIGNAL(itemSelectionChanged()), this, SLOT(refreshTextBrowser()));
    }
    previous_news = item;
  }
  RssFeed *stream = listStreams->getRSSItemFromUrl(item->text(NEWS_URL_COL));
  RssArticle* article = stream->getItem(item->text(NEWS_ID));
  QString html;
  html += "<div style='border: 2px solid red; margin-left: 5px; margin-right: 5px; margin-bottom: 5px;'>";
  html += "<div style='background-color: #678db2; font-weight: bold; color: #fff;'>"+article->getTitle() + "</div>";
  if(article->getDate().isValid()) {
    html += "<div style='background-color: #efefef;'><b>"+tr("Date: ")+"</b>"+article->getDate().toLocalTime().toString(Qt::SystemLocaleLongDate)+"</div>";
  }
  if(!article->getAuthor().isEmpty()) {
    html += "<div style='background-color: #efefef;'><b>"+tr("Author: ")+"</b>"+article->getAuthor()+"</div>";
  }
  html += "</div>";
  html += "<divstyle='margin-left: 5px; margin-right: 5px;'>"+article->getDescription()+"</div>";
  textBrowser->setHtml(html);
  article->setRead();
  item->setData(NEWS_TITLE_COL, Qt::ForegroundRole, QVariant(QColor("grey")));
  item->setData(0, Qt::DecorationRole, QVariant(QIcon(":/Icons/sphere.png")));
  // Decrement feed nb unread news
  updateItemInfos(listStreams->getUnreadItem());
  updateItemInfos(listStreams->getTreeItemFromUrl(item->text(NEWS_URL_COL)));
}

void RSSImp::saveSlidersPosition() {
  // Remember sliders positions
  QIniSettings settings("qBittorrent", "qBittorrent");
  settings.setValue("rss/splitter_h", splitter_h->saveState());
  settings.setValue("rss/splitter_v", splitter_v->saveState());
  qDebug("Splitters position saved");
}

void RSSImp::restoreSlidersPosition() {
  QIniSettings settings("qBittorrent", "qBittorrent");
  QByteArray pos_h = settings.value("rss/splitter_h", QByteArray()).toByteArray();
  if(!pos_h.isNull()) {
    splitter_h->restoreState(pos_h);
  }
  QByteArray pos_v = settings.value("rss/splitter_v", QByteArray()).toByteArray();
  if(!pos_v.isNull()) {
    splitter_v->restoreState(pos_v);
  }
}

void RSSImp::updateItemsInfos(QList<QTreeWidgetItem *> items) {
  foreach(QTreeWidgetItem* item, items) {
    updateItemInfos(item);
  }
}

void RSSImp::updateItemInfos(QTreeWidgetItem *item) {
  RssFile *rss_item = listStreams->getRSSItem(item);
  QString name;
  if(rss_item == rssmanager)
    name = tr("Unread");
  else
    name = rss_item->getName();
  item->setText(0, name + QString::fromUtf8("  (") + QString::number(rss_item->getNbUnRead(), 10)+ QString(")"));
  // If item has a parent, update it too
  if(item->parent())
    updateItemInfos(item->parent());
}

void RSSImp::updateFeedIcon(QString url, QString icon_path){
  QTreeWidgetItem *item = listStreams->getTreeItemFromUrl(url);
  item->setData(0,Qt::DecorationRole, QVariant(QIcon(icon_path)));
}

void RSSImp::updateFeedInfos(QString url, QString aliasOrUrl, unsigned int nbUnread){
  QTreeWidgetItem *item = listStreams->getTreeItemFromUrl(url);
  RssFeed *stream = (RssFeed*)listStreams->getRSSItem(item);
  item->setText(0, aliasOrUrl + QString::fromUtf8("  (") + QString::number(nbUnread, 10)+ QString(")"));
  if(!stream->isLoading())
    item->setData(0,Qt::DecorationRole, QVariant(QIcon(stream->getIconPath())));
  // Update parent
  if(item->parent())
    updateItemInfos(item->parent());
  // Update Unread item
  updateItemInfos(listStreams->getUnreadItem());
  // If the feed is selected, update the displayed news
  if(listStreams->currentItem() == item ){
    refreshNewsList(item);
  } else {
    // Update unread items
    if(listStreams->currentItem() == listStreams->getUnreadItem()) {
      refreshNewsList(listStreams->getUnreadItem());
    }
  }
}

void RSSImp::updateRefreshInterval(unsigned int val) {
  rssmanager->updateRefreshInterval(val);
}

RSSImp::RSSImp(QWidget *parent) : QWidget(parent) {
  setupUi(this);

  rssmanager = RssManager::instance();

  listStreams = new FeedListWidget(splitter_h, rssmanager);
  splitter_h->insertWidget(0, listStreams);
  listNews->hideColumn(NEWS_URL_COL);
  listNews->setColumnWidth(0, 16);
  listNews->setSelectionBehavior(QAbstractItemView::SelectItems);
  listNews->setSelectionMode(QAbstractItemView::SingleSelection);

  fillFeedsList();
  refreshNewsList(listStreams->currentItem());

  loadFoldersOpenState();
  connect(rssmanager, SIGNAL(feedInfosChanged(QString, QString, unsigned int)), this, SLOT(updateFeedInfos(QString, QString, unsigned int)));
  connect(rssmanager, SIGNAL(feedIconChanged(QString, QString)), this, SLOT(updateFeedIcon(QString, QString)));

  connect(listStreams, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(displayRSSListMenu(const QPoint&)));
  connect(listNews, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(displayItemsListMenu(const QPoint&)));

  // Feeds list actions
  connect(actionDelete, SIGNAL(triggered()), this, SLOT(deleteSelectedItems()));
  connect(actionRename, SIGNAL(triggered()), this, SLOT(renameFiles()));
  connect(actionUpdate, SIGNAL(triggered()), this, SLOT(refreshSelectedItems()));
  connect(actionNew_folder, SIGNAL(triggered()), this, SLOT(askNewFolder()));
  connect(actionNew_subscription, SIGNAL(triggered()), this, SLOT(on_newFeedButton_clicked()));
  connect(actionUpdate_all_feeds, SIGNAL(triggered()), this, SLOT(on_updateAllButton_clicked()));
  connect(actionCopy_feed_URL, SIGNAL(triggered()), this, SLOT(copySelectedFeedsURL()));
  connect(actionMark_items_read, SIGNAL(triggered()), this, SLOT(on_markReadButton_clicked()));
  // News list actions
  connect(actionOpen_news_URL, SIGNAL(triggered()), this, SLOT(openNewsUrl()));
  connect(actionDownload_torrent, SIGNAL(triggered()), this, SLOT(downloadTorrent()));

  connect(listStreams, SIGNAL(currentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)), this, SLOT(refreshNewsList(QTreeWidgetItem*)));
  connect(listStreams, SIGNAL(foldersAltered(QList<QTreeWidgetItem*>)), this, SLOT(updateItemsInfos(QList<QTreeWidgetItem*>)));
  connect(listStreams, SIGNAL(overwriteAttempt(QString)), this, SLOT(displayOverwriteError(QString)));

  connect(listNews, SIGNAL(itemSelectionChanged()), this, SLOT(refreshTextBrowser()));
  connect(listNews, SIGNAL(itemDoubleClicked(QTreeWidgetItem *, int)), this, SLOT(downloadTorrent()));

  // Refresh all feeds
  rssmanager->refreshAll();
  // Restore sliders position
  restoreSlidersPosition();
  // Bind saveSliders slots
  connect(splitter_v, SIGNAL(splitterMoved(int, int)), this, SLOT(saveSlidersPosition()));
  connect(splitter_h, SIGNAL(splitterMoved(int, int)), this, SLOT(saveSlidersPosition()));

  qDebug("RSSImp constructed");
}

RSSImp::~RSSImp(){
  qDebug("Deleting RSSImp...");
  saveFoldersOpenState();
  delete listStreams;
  RssManager::drop();
  qDebug("RSSImp deleted");
}


void RSSImp::on_settingsButton_clicked() {
  RssSettingsDlg dlg(this);
  if(dlg.exec())
    updateRefreshInterval(Preferences::getRefreshInterval());
}

void RSSImp::on_rssDownloaderBtn_clicked()
{
  AutomatedRssDownloader dlg(this);
  dlg.exec();
  if(dlg.isRssDownloaderEnabled())
    on_updateAllButton_clicked();
}