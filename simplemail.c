/***************************************************************************
 SimpleMail - Copyright (C) 2000 Hynek Schlawack and Sebastian Bauer

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
***************************************************************************/

/*
** simplemail.c
*/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "addressbook.h"
#include "codesets.h"
#include "configuration.h"
#include "estimate.h"
#include "filter.h"
#include "folder.h"
#include "imap.h" /* imap_thread_xxx() */
#include "mail.h"
#include "simplemail.h"
#include "smintl.h"
#include "spam.h"
#include "support_indep.h"
#include "status.h"
#include "trans.h"

#include "addressbookwnd.h"
#include "composewnd.h"
#include "configwnd.h"
#include "filterwnd.h"
#include "folderwnd.h"
#include "gui_main.h"
#include "mainwnd.h"
#include "parse.h"
#include "readwnd.h"
#include "searchwnd.h"
#include "subthreads.h"
#include "support.h"
#include "tcpip.h"
#include "upwnd.h"

/* the current mail should be viewed, returns the number of the window
	which the function has opened or -1 for an error */
int callback_read_active_mail(void)
{
	char *filename;
	struct mail *m;
	struct folder *f;

	if (!(f = main_get_folder())) return -1;
	if (!(filename = main_get_mail_filename())) return -1;
	if (!(m = main_get_active_mail())) return -1;

	return callback_read_mail(f,m,-1);
}


int callback_read_mail(struct folder *f, struct mail *mail, int window)
{
	int num;

	if (!f) f = folder_find_by_mail(mail);
	if (!f) return -1;

	if (mail->flags & MAIL_FLAGS_PARTIAL)
	{
		imap_download_mail(f,mail);
		main_refresh_mail(mail);
	}

	num = read_window_open(f->path, mail, window);
	if (num >= 0)
	{
		if (mail_get_status_type(mail) == MAIL_STATUS_UNREAD)
		{
			folder_set_mail_status(f,mail, MAIL_STATUS_READ | (mail->status & (~MAIL_STATUS_MASK)));
			if (mail->flags & MAIL_FLAGS_NEW && f->new_mails) f->new_mails--;
			mail->flags &= ~MAIL_FLAGS_NEW;
			main_refresh_mail(mail);
			main_refresh_folder(f);
		}
	}
	return num;
}

/* the selected mails should be deleted */
void callback_delete_mails(void)
{
	struct folder *from_folder = main_get_folder();
	struct mail *mail;
	void *handle;
	int num;
	int permanent; /* 1 if mails should be deleted permanently */

	if (from_folder)
	{
		int search_has;

		/* Check if folder is not used */
		if (!folder_attempt_lock(from_folder))
		{
			sm_request(NULL,_("Cannot delete mails, because folder is in use.\n"),_("Ok"));
			return;
		}

		/* Count the number of selected mails first */
		mail = main_get_mail_first_selected(&handle);
		num = 0;
		while (mail)
		{
			num++;
			mail = main_get_mail_next_selected(&handle);
		}

		if (!num) return;
		if (from_folder == folder_deleted())
		{
			char buf[256];
			if (num == 1) strcpy(buf,_("Do you really want to delete the selected mail permanently?"));
			else sprintf(buf,_("Do you really want to delete %d mails permanently?"),num);
			if (!sm_request(NULL,buf,_("_Yes|_No"))) return;
			permanent = 1;
		} else permanent = 0;

		if (from_folder->is_imap)
		{
			mail = main_get_mail_first_selected(&handle);
			while (mail)
			{
				if (mail_is_marked_as_deleted(mail)) folder_mark_undeleted(from_folder,mail);
				else folder_mark_deleted(from_folder,mail);
				mail = main_get_mail_next_selected(&handle);
			}
			main_refresh_mails_selected();
		} else
		{
			search_has = search_has_mails();

			mail = main_get_mail_first_selected(&handle);
			while (mail)
			{
				if (search_has) search_remove_mail(mail);
				if (permanent) folder_delete_mail(from_folder,mail);
				else folder_move_mail(from_folder,folder_deleted(),mail);
				mail = main_get_mail_next_selected(&handle);
			}
			main_refresh_folder(from_folder);
			if (!permanent) main_refresh_folder(folder_deleted());

			main_remove_mails_selected();
		}

		folder_unlock(from_folder);
	}
}

/* a single mail of any folder should be deleted */
int callback_delete_mail(struct mail *mail)
{
	struct folder *f = folder_find_by_mail(mail);
	struct folder *fd = folder_deleted();
	if (f)
	{
		if (!folder_attempt_lock(f))
		{
			sm_request(NULL,_("Cannot delete mails, because folder is in use.\n"),_("Ok"));
			return 0;
		}

		if (f != fd)
		{
			folder_move_mail(f,fd,mail);
			main_refresh_folder(f);
		} else folder_delete_mail(f,mail);
		main_refresh_folder(fd);
		if (main_get_folder() == f) main_remove_mail(mail);
		if (search_has_mails()) search_remove_mail(mail);
		folder_unlock(f);
		return 1;
	}
	return 0;
}

/* delete mails by uid and folder */
void callback_delete_mail_by_uid(char *server, char *path, unsigned int uid)
{
	struct folder *f;
	struct mail *mail;

	folders_lock();

	f = folder_find_by_imap(server,path);
	if (!f)
	{
		folders_unlock();
		return;
	}

	folder_lock(f);

	mail = folder_imap_find_mail_by_uid(f, uid);
	if (!mail)
	{
		folder_unlock(f);
		folders_unlock();
		return;
	}

//  folder_delete_mail(f,mail);
	folder_move_mail(f,folder_deleted(),mail);
  main_refresh_folder(f);
  main_refresh_folder(folder_deleted());

	if (main_get_folder() == f) main_remove_mail(mail);
	if (search_has_mails()) search_remove_mail(mail);

	folder_unlock(f);
	folders_unlock();
}

/* get the address */
void callback_get_address(void)
{
	struct mail *mail = main_get_active_mail();
	if (mail)
	{
		char *addr;
		struct folder *f;

		f = main_get_folder();

		if (folder_get_type(f) == FOLDER_TYPE_SEND) addr = mail->to_addr;
		else addr = mail->from_addr;

		if (addr)
		{
			if (!addressbook_get_realname(addr))
			{
				addressbook_open_with_new_address_from_mail(mail,folder_get_type(f) == FOLDER_TYPE_SEND);
			}
		}
	}
}

int callback_write_mail(char *from, char *to, char *replyto, char *subject)
{
	struct compose_args ca;
	int win_num;
	memset(&ca,0,sizeof(ca));

	ca.action = COMPOSE_ACTION_NEW;
	ca.to_change = mail_create_for(from,to,replyto,subject);

	win_num = compose_window_open(&ca);

	if (ca.to_change) mail_free(ca.to_change);
	return win_num;
}

/* a new mail should be written to the given address */
void callback_write_mail_to(struct addressbook_entry *address)
{
	char *to_str = addressbook_get_address_str(address);
	callback_write_mail_to_str(to_str,NULL);
	free(to_str);
}

/* a new mail should be written to a given address string */
int callback_write_mail_to_str(char *str, char *subject)
{
	return callback_write_mail(NULL,str,NULL,subject);
}

/* a new mail should be composed */
void callback_new_mail(void)
{
	struct folder *f = main_get_folder();
	if (!f) return;
	callback_write_mail(f->def_from,f->def_to,f->def_replyto,NULL);
}

/* reply this mail */
void callback_reply_mails(char *folder_path, int num, struct mail **to_reply_array)
{
	struct folder *f = folder_find_by_path(folder_path);
	struct mail **mail_array;
	char buf[380];
	int i;

	if (!f) return;

	/* Download the mails if needed */
	for (i=0;i<num;i++)
	{
		if (to_reply_array[i]->flags & MAIL_FLAGS_PARTIAL)
		{
			imap_download_mail(f,to_reply_array[i]);
			main_refresh_mail(to_reply_array[i]);
		}
	}

	if (getcwd(buf, sizeof(buf)) == NULL) return;

	if ((mail_array = malloc(num*sizeof(struct mail *))))
	{
		struct mail *reply;
		int err = 0;

		chdir(folder_path);

		for (i=0;i<num;i++)
		{
			if (!(mail_array[i] = mail_create_from_file(to_reply_array[i]->filename)))
			{
				err = 1;
				break;
			}
		}

		if (!err)
		{
			for (i=0;i<num;i++)
			{
				/* we are already changed into the directory */
				mail_read_contents(NULL,mail_array[i]);
			}
		}

		chdir(buf);

		if (!err)
		{
			if ((reply = mail_create_reply(num,mail_array)))
			{
				struct compose_args ca;
				memset(&ca,0,sizeof(ca));
				ca.to_change = reply;
				ca.action = COMPOSE_ACTION_REPLY;
				ca.ref_mail = to_reply_array[0];
				compose_window_open(&ca);

				mail_free(reply);
			}
		}

		for (i=0;i<num;i++)
		{
			if (mail_array[i]) mail_free(mail_array[i]);
			else break;
		}
		free(mail_array);
	}
}

/* a mail should be replied */
void callback_reply_selected_mails(void)
{
	struct mail *mail;
	struct mail **mail_array;
	void *handle;
	int num;

	/* Count the number of selected mails first */
	mail = main_get_mail_first_selected(&handle);
	num = 0;
	while (mail)
	{
		num++;
		mail = main_get_mail_next_selected(&handle);
	}

	if (!num) return;

	if ((mail_array = malloc(sizeof(struct mail *)*num)))
	{
		int i = 0;

		mail = main_get_mail_first_selected(&handle);
		while (mail)
		{
			mail_array[i++] = mail;
			mail = main_get_mail_next_selected(&handle);
		}

		callback_reply_mails(main_get_folder_drawer(), num, mail_array);
		free(mail_array);
	}
}

/* a mail should be forwarded */
void callback_forward_mails(char *folder_path, int num, struct mail **to_forward_array)
{
	struct folder *f = folder_find_by_path(folder_path);
	struct mail **mail_array;
	char buf[380];
	int i;

	if (!f) return;

	/* Download the mails if needed */
	for (i=0;i<num;i++)
	{
		if (to_forward_array[i]->flags & MAIL_FLAGS_PARTIAL)
		{
			imap_download_mail(f,to_forward_array[i]);
			main_refresh_mail(to_forward_array[i]);
		}
	}

	getcwd(buf, sizeof(buf));
	if (getcwd(buf, sizeof(buf)) == NULL) return;

	chdir(folder_path);

	if ((mail_array = malloc(num*sizeof(struct mail *))))
	{
		struct mail *forward;
		int err = 0;

		for (i=0;i<num;i++)
		{
			if ((mail_array[i] = mail_create_from_file(to_forward_array[i]->filename)))
			{
				mail_read_contents("",mail_array[i]);
			} else 
			{
				err = 1;
				break;
			}
		}

		if (!err)
		{
			if ((forward = mail_create_forward(num,mail_array)))
			{
				struct compose_args ca;
				memset(&ca,0,sizeof(ca));
				ca.to_change = forward;
				ca.action = COMPOSE_ACTION_FORWARD;
				ca.ref_mail = to_forward_array[0];
				compose_window_open(&ca);

				mail_free(forward);
			}
		}

		for (i=0;i<num;i++)
		{
			if (mail_array[i]) mail_free(mail_array[i]);
			else break;
		}
		free(mail_array);
	}

	chdir(buf);
}

/* a single or multiple mails should be forwarded */
void callback_forward_selected_mails(void)
{
	struct mail *mail;
	struct mail **mail_array;
	void *handle;
	int num;

	/* Count the number of selected mails first */
	mail = main_get_mail_first_selected(&handle);
	num = 0;
	while (mail)
	{
		num++;
		mail = main_get_mail_next_selected(&handle);
	}

	if (!num) return;

	if ((mail_array = malloc(sizeof(struct mail *)*num)))
	{
		int i = 0;

		mail = main_get_mail_first_selected(&handle);
		while (mail)
		{
			mail_array[i++] = mail;
			mail = main_get_mail_next_selected(&handle);
		}

		callback_forward_mails(main_get_folder_drawer(), num, mail_array);
		free(mail_array);
	}
}

/* This is a helper function used by several other move functions. Its purpose is to move
   a mail from a folder to another with support for IMAP */
static int move_mail_helper(struct mail *mail, struct folder *from_folder, struct folder *dest_folder)
{
	int same_server = folder_on_same_imap_server(from_folder,dest_folder); /* is 0 if local only */
	int success = 0;

	if (!same_server)
	{
		if (mail->flags & MAIL_FLAGS_PARTIAL)
		{
			imap_download_mail(from_folder,mail);
			main_refresh_mail(mail);
		}

		if (dest_folder->is_imap)
		{
			if (imap_append_mail(mail, from_folder->path, dest_folder))
			{
				if (from_folder->is_imap)
					imap_delete_mail_by_filename(mail->filename,from_folder);

				folder_delete_mail(from_folder,mail);
				success = 1;
			}
		} else
		if (from_folder->is_imap)
		{
			char *filename = mystrdup(mail->filename);
			if (filename)
			{
				/* folder_move_mail() might change the filename */
				if (folder_move_mail(from_folder,dest_folder,mail))
				{
					imap_delete_mail_by_filename(filename,from_folder);
					success = 1;
				}
				free(filename);
			}
		} else
		{
			success = folder_move_mail(from_folder,dest_folder,mail);
		}
	} else
	{
		/* EMail should be moved on the same imap server */
		if (imap_move_mail(mail,from_folder,dest_folder))
		{
			folder_delete_mail(from_folder,mail);
			success = 1;
		}
	}
	return success;
}

/* a single mail should be moved from a folder to another folder */
void callback_move_mail(struct mail *mail, struct folder *from_folder, struct folder *dest_folder)
{
	if (from_folder != dest_folder)
	{
		folder_move_mail(from_folder,dest_folder,mail);

		/* If outgoing folder is visible remove the mail */
		if (main_get_folder() == from_folder)
			main_remove_mail(mail);

		/* If sent folder is visible insert the mail */
		if (main_get_folder() == dest_folder)
			main_insert_mail_pos(mail,folder_get_index_of_mail(dest_folder, mail)-1);

		main_refresh_folder(from_folder);
		main_refresh_folder(dest_folder);
	}
}

/* mails has been droped onto the folder */
void callback_maildrop(struct folder *dest_folder)
{
	struct folder *from_folder = main_get_folder();
	if (from_folder != dest_folder)
	{
		void *handle;
		struct mail *mail;

		mail = main_get_mail_first_selected(&handle);

		while (mail)
		{
			move_mail_helper(mail,from_folder,dest_folder);
			mail = main_get_mail_next_selected(&handle);
		}
		main_refresh_folder(from_folder);
		main_refresh_folder(dest_folder);
		main_remove_mails_selected();
	}
}

/* a single mail should be moved */
int callback_move_mail_request(char *folder_path, struct mail *mail)
{
	struct folder *src_folder = folder_find_by_path(folder_path);
	struct folder *dest_folder;

	if (!src_folder) return 0;

	if ((dest_folder = sm_request_folder(_("Please select the folder where to move the mails"),src_folder)))
	{
		if (move_mail_helper(mail,src_folder,dest_folder))
		{
			main_remove_mail(mail);
			main_refresh_folder(src_folder);
			main_refresh_folder(dest_folder);

			if (main_get_folder() == dest_folder)
				main_insert_mail(mail);

			return 1;
		}
	}
	return 0;
}

/* all selected mails should be moved */
void callback_move_selected_mails(void)
{
	struct mail *mail;
	void *handle;
	int num;
	struct folder *src_folder;
	struct folder *dest_folder;

	if (!(src_folder = main_get_folder())) return;

	/* Count the number of selected mails first */
	mail = main_get_mail_first_selected(&handle);
	num = 0;
	while (mail)
	{
		num++;
		mail = main_get_mail_next_selected(&handle);
	}

	if (!num) return;

	if ((dest_folder = sm_request_folder(_("Please select the folder where to move the mails"),src_folder)))
	{
		if (src_folder != dest_folder)
		{
			struct mail *mail = main_get_mail_first_selected(&handle);
			handle = NULL;
			while (mail)
			{
				move_mail_helper(mail,src_folder,dest_folder);
				mail = main_get_mail_next_selected(&handle);
			}
			main_refresh_folder(src_folder);
			main_refresh_folder(dest_folder);
			main_remove_mails_selected();
		}
	}
}

/* Process the current selected folder and mark all mails which are identified as spam */
void callback_check_selected_folder_for_spam(void)
{
	struct folder *folder = main_get_folder();
	void *handle = NULL;
	struct mail *m;

	while ((m = folder_next_mail(folder, &handle)))
	{
		if (m->flags & MAIL_FLAGS_PARTIAL)
			imap_download_mail(folder,m);

		if (spam_is_mail_spam(folder,m))
		{
			folder_set_mail_status(folder,m,MAIL_STATUS_SPAM);
			m->flags &= ~MAIL_FLAGS_NEW;
			main_refresh_mail(m);
		}
	}
}

/* Move all mails marked as spam into the spam folder */
void callback_move_spam_marked_mails(void)
{
	struct folder *folder = main_get_folder();
	struct folder *spam_folder = folder_spam();
	struct mail **mail_array;
	struct mail *m;
	int i;

	if (folder == spam_folder) return;
	if (!spam_folder) return;

	if ((mail_array = folder_query_mails(folder, FOLDER_QUERY_MAILS_PROP_SPAM)))
	{
		i = 0;
		while ((m = mail_array[i]))
		{
			callback_move_mail(m, folder, spam_folder);
			i++;
		}
		free(mail_array);
	}
}

/* the currently selected mail should be changed */
void callback_change_mail(void)
{
	char *filename;

	if ((filename = main_get_mail_filename()))
	{
		struct mail *mail;
		char buf[256];

		getcwd(buf, sizeof(buf));
		chdir(main_get_folder_drawer());

		if ((mail = mail_create_from_file(filename)))
		{
			struct compose_args ca;
			mail_read_contents("",mail);
			memset(&ca,0,sizeof(ca));
			ca.to_change = mail;
			ca.action = COMPOSE_ACTION_EDIT;
			compose_window_open(&ca);
			mail_free(mail);
		}

		chdir(buf);
	}
}

/* mails should be fetched */
void callback_fetch_mails(void)
{
	mails_dl(0);
}

/* mails should be sent */
void callback_send_mails(void)
{
	mails_upload();
}

/* Check the mails of a single acount */
void callback_check_single_account(int account_num)
{
	struct account *ac = (struct account*)list_find(&user.config.account_list,account_num);
	if (ac)
	{
		mails_dl_single_account(ac);
	}
}

/* open the search window */
void callback_search(void)
{
	struct folder *f = main_get_folder();
	search_open(f->name);
}

/* Start the search process with the given search options */
void callback_start_search(struct search_options *so)
{
	search_clear_results();
	folder_start_search(so);
}

/* Stop the search process */
void callback_stop_search(void)
{
	thread_abort(NULL);
}

/* filter the mails */
void callback_filter(void)
{
	struct folder *f = main_get_folder();
	if (f)
	{
		main_freeze_mail_list();
		folder_filter(f);
		main_thaw_mail_list();
	}
}

/* the filters should be edited */
void callback_edit_filter(void)
{
	filter_open();
}

/* addressbook should be opened */
void callback_addressbook(void)
{
	addressbook_open();
}

/* Open the configuration window */
void callback_config(void)
{
	open_config();
}

/* a new folder has been activated */
void callback_folder_active(void)
{
	struct folder *folder = main_get_folder();
	if (folder)
	{
		if (folder->is_imap)
		{
			imap_thread_connect(folder);
		}
		main_set_folder_mails(folder);
	}
}

/* a new mail should be added to a given folder */
struct mail *callback_new_mail_to_folder(char *filename, struct folder *folder)
{
	int pos;
	char buf[256];
	struct mail *mail = NULL;

	if (!folder) return NULL;

	getcwd(buf, sizeof(buf));
	chdir(folder->path);

	if (!sm_file_is_in_drawer(filename,folder->path))
	{
		char *newname;

		if ((newname = mail_get_new_name(MAIL_STATUS_UNREAD)))
		{
			myfilecopy(filename,newname);
			mail = mail_create_from_file(newname);
			free(newname);
		}
	} else
	{
		mail = mail_create_from_file(filename);
	}

	if (mail)
	{
		pos = folder_add_mail(folder,mail,1);
		if (main_get_folder() == folder && pos != -1)
			main_insert_mail_pos(mail,pos-1);

		main_refresh_folder(folder);
	}

	chdir(buf);
	return mail;
}

/* a new mail has arrived */
static void callback_new_mail_arrived(struct mail *mail, struct folder *folder)
{
	struct filter *f;
	int pos;

	mail->flags |= MAIL_FLAGS_NEW;
	pos = folder_add_mail(folder,mail,1);
	if (main_get_folder() == folder && pos != -1)
	{
		main_insert_mail_pos(mail,pos-1);
	}

	/* This has to be optmized! */
	if ((f = folder_mail_can_be_filtered(folder, mail, 1)))
	{
		if (f->use_dest_folder && f->dest_folder)
		{
			struct folder *dest_folder = folder_find_by_name(f->dest_folder);
			if (dest_folder)
			{
				/* very slow, because the sorted array is rebuilded in the both folders! */
				callback_move_mail(mail, folder, dest_folder);
			}
		}
	}

	main_refresh_folder(folder);
}

struct export_data
{
	char *filename;
	char *foldername;
};

/* Entry point for export thread */
static int export_entry(struct export_data *data)
{
	char *filename;
	char *foldername;

	if ((filename = mystrdup(data->filename)))
	{
		if ((foldername = mystrdup(data->foldername)))
		{
			if (thread_parent_task_can_contiue())
			{
				struct folder *f;

				/* lock folder list */
				folders_lock();
				f = folder_find_by_name(foldername);
				if (f)
				{
					FILE *fh;
					char head_buf[300];

					/* now lock the folder */
					folder_lock(f);
					/* unlock the folder list */
					folders_unlock();

					sprintf(head_buf, _("Exporting folder %s to %s"),f->name,filename);
					thread_call_parent_function_async(status_init,1,0);
					thread_call_parent_function_async_string(status_set_title,1,_("SimpleMail - Exporting folder"));
					thread_call_parent_function_async_string(status_set_head,1,head_buf);
					thread_call_parent_function_async(status_open,0);

					if ((fh = fopen(filename,"w")))
					{
						void *handle = NULL;
						char buf[384];
						struct mail *m;
						char *file_buf;
						int max_size = 0;
						int size = 0;
						int mail_no = 1;

						while ((m = folder_next_mail(f, &handle)))
							max_size += m->size;

						thread_call_parent_function_async(status_init_gauge_as_bytes,1,max_size);
						thread_call_parent_function_async(status_init_mail, 1, f->num_mails);

						if ((file_buf = malloc(8192)))
						{
							getcwd(buf, sizeof(buf));
							chdir(f->path);

							handle = NULL;
							while ((m = folder_next_mail(f, &handle)))
							{
								FILE *in;

								thread_call_parent_function_async(status_set_mail, 2, mail_no, m->size);

								fprintf(fh, "From %s\n",m->from_addr?m->from_addr:"");
			
								in = fopen(m->filename,"r");
								if (in)
								{
									while (!feof(in))
									{
										int blocks = fread(file_buf,1,8192,in);
										size += fwrite(file_buf,1,blocks,fh);

										thread_call_parent_function_async(status_set_gauge,1,size);
									}
									fclose(in);
								}
								fputs("\n",fh);
								mail_no++;
							}
							free(file_buf);
						}
			
						chdir(buf);
			
						fclose(fh);
					}
					thread_call_parent_function_async(status_close,0);

					folder_unlock(f);
				} else folders_unlock();
			}
			free(foldername);
		}
		free(filename);
	}
	return 0;
}

/* Export mails */
void callback_export(void)
{
	struct folder *f;
	char *filename;

	if (!(f = main_get_folder())) return;

	filename = sm_request_file(_("Choose export filename"), "",1);
	if (filename && *filename)
	{
		struct export_data data;

		data.filename = filename;
		data.foldername = f->name;

		if (!(thread_start(THREAD_FUNCTION(export_entry),&data)))
		{
			sm_request(NULL,_("Couldn't start process for exporting.\n"),_("Ok"));
		}
	}
	free(filename);
}

/* a new mail has been arrived, only the filename is given */
void callback_new_mail_arrived_filename(char *filename)
{
	struct mail *mail;
	char buf[256];

	getcwd(buf, sizeof(buf));
	chdir(folder_incoming()->path);

	if ((mail = mail_create_from_file(filename)))
		callback_new_mail_arrived(mail,folder_incoming());

	chdir(buf);
}

/* a new mail has been arrived into an imap folder */
void callback_new_imap_mail_arrived(char *filename, char *server, char *path)
{
	struct mail *mail;
	struct folder *f;
	char buf[256];

	f = folder_find_by_imap(server,path);
	if (!f) return;

	getcwd(buf, sizeof(buf));
	chdir(f->path);

	if ((mail = mail_create_from_file(filename)))
		callback_new_mail_arrived(mail,f);

	chdir(buf);
}

/* After downloading this function is called */
void callback_number_of_mails_downloaded(int num)
{
	if (num && user.config.receive_sound)
	{
		sm_play_sound(user.config.receive_sound_file);
	}

  if (num && user.config.receive_arexx)
  {
  	gui_execute_arexx(user.config.receive_arexx_file);
  }
}

/* a new mail has been written */
void callback_new_mail_written(struct mail *mail)
{
	folder_add_mail(folder_outgoing(),mail,1);
	if (main_get_folder() == folder_outgoing())
	{
		main_insert_mail(mail);
	}
	main_refresh_folder(folder_outgoing());
}

/* a mail has been send so it can be moved to the "Sent" drawer now */
void callback_mail_has_been_sent(char *filename)
{
	struct filter *f;
	struct folder *out = folder_outgoing();
	struct folder *sent = folder_sent();
	struct mail *m;
	if (!out || !sent) return;

	if ((m = folder_find_mail_by_filename(out,filename)))
	{
		/* set the new mail status */
		folder_set_mail_status(out,m,MAIL_STATUS_SENT);
		callback_move_mail(m,out,sent);

		/* This has to be optmized! */
		if ((f = folder_mail_can_be_filtered(folder_sent(), m, 2)))
		{
			if (f->use_dest_folder && f->dest_folder)
			{
				struct folder *dest_folder = folder_find_by_name(f->dest_folder);
				if (dest_folder)
				{
					/* very slow, because the sorted array is rebuilded in the both folders! */
					callback_move_mail(m, folder_sent(), dest_folder);
				}
			}
		}
	}
}

/* adds a new imap folder */
void callback_add_imap_folder(char *server, char *path)
{
	struct folder *folder;

	folders_lock();
	if ((folder = folder_find_by_imap(server,"")))
	{
		folder_add_imap(folder, path);
	}
	folders_unlock();
}

/* a mail has been changed/replaced by the user */
void callback_mail_changed(struct folder *folder, struct mail *oldmail, struct mail *newmail)
{
	if (main_get_folder() == folder)
	{
		if (search_has_mails()) search_remove_mail(oldmail);
		main_replace_mail(oldmail, newmail);
	}
}

/* mark/unmark all selected mails */
void callback_mails_mark(int mark)
{
	struct folder *folder = main_get_folder();
	struct mail *mail;
	void *handle;
	if (!folder) return;

	mail = main_get_mail_first_selected(&handle);
	while (mail)
	{
		int new_status;

		if (mark) new_status = mail->status | MAIL_STATUS_FLAG_MARKED;
		else new_status = mail->status & (~MAIL_STATUS_FLAG_MARKED);

		if (new_status != mail->status)
		{
			folder_set_mail_status(folder,mail,new_status);
			main_refresh_mail(mail);
		}

		mail = main_get_mail_next_selected(&handle);
	}
}

/* set the status of all selected mails */
void callback_mails_set_status(int status)
{
	struct folder *folder = main_get_folder();
	struct mail *mail;
	void *handle;
	if (!folder) return;

	mail = main_get_mail_first_selected(&handle);
	while (mail)
	{
		int new_status = mail->status;

		if (status == MAIL_STATUS_HOLD || status == MAIL_STATUS_WAITSEND)
		{
			if (!(mail->flags & MAIL_FLAGS_NORCPT))
			{
				/* Only change the status if mail has an recipient. All new mails with no recipient
				 * Will automatically get the hold state */
				if (mail_get_status_type(mail) == MAIL_STATUS_HOLD || mail_get_status_type(mail) == MAIL_STATUS_WAITSEND || mail_get_status_type(mail) == MAIL_STATUS_SENT)
					new_status = status;
			}
		} else
		{
			if (status == MAIL_STATUS_READ || status == MAIL_STATUS_UNREAD)
			{
				if (mail_get_status_type(mail) == MAIL_STATUS_READ || mail_get_status_type(mail) == MAIL_STATUS_UNREAD)
					new_status = status;
			}
		}

		new_status |= mail->status & MAIL_STATUS_FLAG_MARKED;

		if (new_status != mail->status)
		{
			folder_set_mail_status(folder,mail,new_status);
			main_refresh_mail(mail);
			main_refresh_folder(folder);
		}

		mail = main_get_mail_next_selected(&handle);
	}	
}

/* Selected mails are spam */
void callback_selected_mails_are_spam(void)
{
	struct folder *folder = main_get_folder();
	struct mail *mail;
	void *handle;
	if (!folder) return;
	mail = main_get_mail_first_selected(&handle);

	while (mail)
	{
		if (mail->flags & MAIL_FLAGS_PARTIAL)
			imap_download_mail(folder,mail);

		if (spam_feed_mail_as_spam(folder,mail))
		{
			folder_set_mail_status(folder,mail,MAIL_STATUS_SPAM);
			mail->flags &= ~MAIL_FLAGS_NEW;
			main_refresh_mail(mail);
		}
		mail = main_get_mail_next_selected(&handle);
	}	
}

/* Selected mails are ham */
void callback_selected_mails_are_ham(void)
{
	struct folder *folder = main_get_folder();
	struct mail *mail;
	void *handle;
	if (!folder) return;
	mail = main_get_mail_first_selected(&handle);

	while (mail)
	{
		if (mail->flags & MAIL_FLAGS_PARTIAL)
			imap_download_mail(folder,mail);

		if (spam_feed_mail_as_ham(folder,mail))
		{
			if (mail_is_spam(mail))
			{
				folder_set_mail_status(folder,mail,MAIL_STATUS_UNREAD);
				main_refresh_mail(mail);
			}
		}
		mail = main_get_mail_next_selected(&handle);
	}	
}

/* Check if selected mails are spam */
void callback_check_selected_mails_if_spam(void)
{
	struct folder *folder = main_get_folder();
	struct mail *mail;
	void *handle;
	if (!folder) return;
	mail = main_get_mail_first_selected(&handle);

	while (mail)
	{
		if (mail->flags & MAIL_FLAGS_PARTIAL)
			imap_download_mail(folder,mail);

		if (!mail_is_spam(mail))
		{
			if (spam_is_mail_spam(folder,mail))
			{
				folder_set_mail_status(folder,mail,MAIL_STATUS_SPAM);
				mail->flags &= ~MAIL_FLAGS_NEW;
				main_refresh_mail(mail);
			}
		}
		mail = main_get_mail_next_selected(&handle);
	}
}

/* import a addressbook into SimpleMail, return 1 for success */
int callback_import_addressbook(void)
{
	int rc = 0;
	char *filename;
	
	filename = sm_request_file(_("Select an addressbook-file."), "PROGDIR:",0);
	if (filename && *filename)
	{
		addressbook_import_file(filename,1);
		main_build_addressbook();
		addressbookwnd_refresh();
		free(filename);
	}
	
	return rc;
}

/* apply a single filter in the active folder */
void callback_apply_folder(struct filter *filter)
{
	struct folder *folder = main_get_folder();
	if (folder)
	{
		main_freeze_mail_list();
		folder_apply_filter(folder,filter);
		main_thaw_mail_list();
	}
}

/* create a new folder */
void callback_new_folder(void)
{
	folder_edit_new_path(new_folder_path());
}

/* create a new group */
void callback_new_group(void)
{
	folder_add_group(_("New Group"));
	main_refresh_folders();
	search_refresh_folders();
	filter_update_folder_list();
}

/* create a new folder */
void callback_new_folder_path(char *path, char *name)
{
	folder_add_with_name(path, name);
	main_refresh_folders();
	search_refresh_folders();
	filter_update_folder_list();
}

/* Remove the selected folder */
void callback_remove_folder(void)
{
	struct folder *f = main_get_folder();
	if (f)
	{
		if (folder_remove(f))
		{
			main_refresh_folders();
			search_refresh_folders();
			filter_update_folder_list();

			f = main_get_folder();
			main_set_folder_mails(f);
		}
	}
}

/* called when imap folders has been received */
static void callback_received_imap_folders(struct imap_server *server, struct list *all_folder_list, struct list *sub_folder_list)
{
	struct folder *f = folder_find_by_imap(server->name,"");
	if (!f) return;
	folder_imap_set_folders(f, all_folder_list, sub_folder_list);
	folder_fill_lists(all_folder_list, sub_folder_list);
	folder_config_save(f);
}

/*  */
void callback_imap_get_folders(struct folder *f)
{
	if (f->is_imap && f->special == FOLDER_SPECIAL_GROUP)
	{
		struct imap_server *server = account_find_imap_server_by_folder(f);
		if (server)
		{
			imap_get_folder_list(server,callback_received_imap_folders);
			return;
		}
	}
}

/* */
void callback_imap_submit_folders(struct folder *f, struct list *list)
{
	if (f->is_imap && f->special == FOLDER_SPECIAL_GROUP)
	{
		struct imap_server *server = account_find_imap_server_by_folder(f);
		if (server)
		{
			imap_submit_folder_list(server,list);
			return;
		}

	}
}

/* edit folder settings */
void callback_edit_folder(void)
{
	struct folder *from_folder = main_get_folder();
	if (from_folder)
	{
		folder_edit(from_folder);
	}
}

/* set the attributes of a folder like in the folder window */
void callback_change_folder_attrs(void)
{
	struct folder *f = folder_get_changed_folder();
	int refresh;

	if (!f) return;

	if (folder_set_would_need_reload(f, folder_get_changed_name(), folder_get_changed_path(), folder_get_changed_type(), folder_get_changed_defto()))
	{
		/* Remove the mails, as they get's geloaded */
		main_clear_folder_mails();
		refresh = 1;
	} else refresh = 0;

	if (folder_set(f, folder_get_changed_name(), folder_get_changed_path(), folder_get_changed_type(), folder_get_changed_defto(),
										folder_get_changed_deffrom(), folder_get_changed_defreplyto(), folder_get_changed_primary_sort(), folder_get_changed_secondary_sort()))
	{
		if (main_get_folder() == f || refresh)
		{
			main_set_folder_mails(f);
		}
	}

	main_refresh_folder(f);
	search_refresh_folders();
	filter_update_folder_list();
}

/* reload the folders order */
void callback_reload_folder_order(void)
{
	folder_load_order();
	callback_refresh_folders();
}

void callback_refresh_folders(void)
{
	main_refresh_folders();
	search_refresh_folders();
	filter_update_folder_list();
}

/* the configuration has been changed */
void callback_config_changed(void)
{
	/* Build the check single account menu */
	main_build_accounts();
	folder_create_imap();
	callback_refresh_folders();
}

static int autocheck_minutes_start; /* to compare with this */

/* initializes the autocheck function */
void callback_autocheck_refresh(void)
{
	static int called;

	if (user.config.receive_autoonstartup && !called)
	{
		autocheck_minutes_start = 0;
		called = 1;
		callback_timer();
	}
	autocheck_minutes_start = sm_get_current_seconds();
}

/* called every second */
void callback_timer(void)
{
	if (user.config.receive_autocheck)
	{
		if (sm_get_current_seconds() - autocheck_minutes_start > user.config.receive_autocheck * 60)
		{
			/* nothing should happen when mails_dl() is called twice,
			   this could happen if a mail downloading takes very long */
			callback_autocheck_refresh();

			/* If socket library cannot be opened we also shouldn't try
				 to download the mails */
			if (open_socket_lib())
			{
				close_socket_lib();

				if (!user.config.receive_autoifonline || is_online("mi0"))
				{
					mails_dl(1);
				}
			}
		}
	}
}

/* select an mail */
void callback_select_mail(int num)
{
	main_select_mail(num);
}

/* delete all indexfiles */
void callback_delete_all_indexfiles(void)
{
	folder_delete_all_indexfiles();
}

/* rescan the current selected folder */
void callback_rescan_folder(void)
{
	struct folder *f = main_get_folder();
	if (f)
	{
		/* Because this means deleting free all mails we safely remove all found mails as it
     * could reference a old mail */
		search_clear_results();
		folder_rescan(f);
		main_set_folder_mails(f);
		main_refresh_folder(f);
	}
}

int simplemail_main(void)
{
#ifdef ENABLE_NLS
  setlocale(LC_ALL, "");
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);
#endif

	if (!gui_parseargs(0,NULL)) return 0;
	if (!init_threads()) return 0;

	if (codesets_init())
	{
		load_config();
		init_addressbook();
		if (init_folders())
		{
			if (spam_init())
			{
				gui_main(0,NULL);
				folder_delete_deleted();
/*			folder_save_order();*/
				spam_cleanup();
			}
			del_folders();
		}
		codesets_cleanup();
	}
	cleanup_addressbook();
	cleanup_threads();
	return 0;
}

