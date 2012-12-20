#include <gtk/gtk.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "pctv_play_ico.xpm"
#include "pctv_rec_ico.xpm"
#include "pctv_stop_ico.xpm"
#include "pctv_chanselect_ico.xpm"

#define PCTV_GTK_CONFIG_FILE ".pctv_gtk_conf"
#define PCTV_GTK_VERSION "1.4"
#define LINE_LENGTH 512
#define CHAN_LINE_LENGTH 31
#define CHAN_FREQ_LENGTH 8
#define CHAN_NAME_LENGTH CHAN_LINE_LENGTH - CHAN_FREQ_LENGTH - 1
#define MAX_CHANNELS 200
#define MAX_OPTIONS 128
#define ARG_LENGTH 8192
#define ORI_CHANFILE "/usr/local/lib/dune/channels.txt"
#define ORI_XINE_CONF ".xine/config"

typedef struct struct_channels_map {
	gchar name[CHAN_NAME_LENGTH];
	guint32 freq;
} channels_map;

typedef struct struct_channels_map_sel {
	gchar name[CHAN_NAME_LENGTH];
	guint32 freq;
	gboolean selected;
} channels_map_sel;

typedef struct struct_pctv_gtk_config {
	gchar recording_dir[LINE_LENGTH];
	gchar channel_file[LINE_LENGTH];
	gboolean internal_channel_file;
	gboolean channel_load_ori;
	gchar tmp_xineconf[LINE_LENGTH];
	gchar selected_channel[80];
	gchar selected_player[8];
	gchar mplayer_deinterlace[16];
	gchar mplayer_audio_out[16];
	gchar mplayer_video_out[16];
	gboolean mplayer_framedrop;
	gboolean mplayer_postprocess;
	gchar xine_deinterlace[16];
	gchar xine_audio_out[16];
	gchar xine_video_out[16];
	gchar xine_postprocess[16];
	gchar dunerec_input[10];
	gchar dunerec_quality[5];
	gchar dunerec_scan_source[8];
} pctv_gtk_config;

pctv_gtk_config *conf;
channels_map *channel[MAX_CHANNELS];
channels_map_sel *modified_channel[MAX_CHANNELS];
gchar pctv_gtk_version[7], *pctv_gtk_config_file, *homedir, *recording_file;
gint rec_pid = 0;
gboolean mplayer_available = TRUE, xine_available = TRUE;
GtkWidget *win, *xineopt, *mpopt, *cmdline, *status_line, *ext_chanfile_entry, *channel_box;
GtkWidget *channel_table, *modify_table;

void update_cmdline (GtkWidget *cmd_line);

void status (gchar *color, gchar *text) {
	gchar *tmp; tmp = (gchar *) g_malloc (LINE_LENGTH);
	sprintf (tmp, "<span weight=\"bold\" foreground=\"%s\">%s</span>", color, text);
	if (status_line != NULL) gtk_label_set_markup(GTK_LABEL(status_line), tmp); g_free (tmp);
}

GtkWidget *create_statusbar () {
	GtkWidget *hb;
	hb = gtk_hbox_new (TRUE, 0); status_line = gtk_label_new ("");
	gtk_box_pack_start (GTK_BOX (hb), status_line, TRUE, TRUE, 0);
	gtk_widget_show (status_line); status ("blue", "Interface ready! ^_^");
	return hb;
}

void init_channel (channels_map *chan) {
	memset (chan->name, 0, CHAN_NAME_LENGTH); chan->freq = 0;
}

void init_channel_sel (channels_map_sel *chan) {
	memset (chan->name, 0, CHAN_NAME_LENGTH); chan->freq = 0; chan->selected = FALSE;
}

void init_channels (void) {
	gint i;
	for (i = 0; i < MAX_CHANNELS; i++) {
		channel[i] = (channels_map *) g_malloc (sizeof (channels_map)); init_channel (channel[i]);
		modified_channel[i] = (channels_map_sel *) g_malloc (sizeof (channels_map_sel));
		init_channel_sel (modified_channel[i]);
	}
}

void free_channels (void) {
	gint i;
	for (i = 0; i < MAX_CHANNELS; i++) { g_free (channel[i]); g_free (modified_channel[i]); }
}

void pctv_gtk_quit () { g_free (conf); free_channels (); gtk_main_quit (); }

void create_config_defaults (pctv_gtk_config *c) {
	strcpy (pctv_gtk_version, PCTV_GTK_VERSION);
	strcpy (c->recording_dir, "~");
	strcpy (c->channel_file, ORI_CHANFILE);
	c->internal_channel_file = TRUE;
	c->channel_load_ori = TRUE;
	strcpy (c->tmp_xineconf, "/tmp/pctv_gtk_xineconf");
	strcpy (c->selected_channel, "");
	if (xine_available) strcpy (c->selected_player, "Xine");
	else strcpy (c->selected_player, "MPlayer");
	strcpy (c->mplayer_deinterlace, "none");
	strcpy (c->mplayer_audio_out, "alsa");
	strcpy (c->mplayer_video_out, "xv");
	c->mplayer_framedrop = TRUE;
	c->mplayer_postprocess = FALSE;
	strcpy (c->xine_deinterlace, "none");
	strcpy (c->xine_audio_out, "alsa");
	strcpy (c->xine_video_out, "xv");
	strcpy (c->xine_postprocess, "none");
	strcpy (c->dunerec_input, "Antenna");
	strcpy (c->dunerec_quality, "dvd");
	strcpy (c->dunerec_scan_source, "Antenna");
	status ("blue", "Created config defaults..");
}

void save_config (pctv_gtk_config *c) {
	gint fd, i;
	fd = creat (pctv_gtk_config_file, 00640);
	if (fd < 0) status ("red", "Error trying to save config file!");
	else {
		write (fd, pctv_gtk_version, sizeof (pctv_gtk_version));
		write (fd, c, sizeof (pctv_gtk_config));
		for (i = 0; i < MAX_CHANNELS; i++) write (fd, channel[i], sizeof (channels_map));
		close (fd);
	}
	update_cmdline (cmdline);
}

void boja_popup_end (GtkWidget *popup, gchar *what) {
	gtk_widget_destroy(gtk_widget_get_parent (gtk_widget_get_parent (gtk_widget_get_parent (popup))));
	if (what != NULL) {
		if (what == "quit") gtk_main_quit();
	}
}

void boja_popup (gchar *text[], gchar *button_mex, gchar *action) {
	GtkWidget *w, *label, *button;
	w = gtk_dialog_new (); gtk_window_set_title (GTK_WINDOW (w), "Warning");
	while (*text != NULL) {
		label = gtk_label_new (*text);
		gtk_box_pack_start (GTK_BOX (GTK_DIALOG (w)->vbox), label, TRUE, TRUE, 0);
		gtk_widget_show (label); text++;
	}
	if (button_mex == NULL) button = gtk_button_new_with_label ("Ok");
	else button = gtk_button_new_with_label (button_mex);
	g_signal_connect (button, "clicked", G_CALLBACK(boja_popup_end), action);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (w)->action_area), button, FALSE, FALSE, 0);
	gtk_widget_show (button); gtk_widget_show (w);
}

int ver_to_num (gchar *ver) {
	gint l = strlen(ver); gchar *t;
	gchar *num = (gchar *) g_malloc(l + 1); memset(num, 0, l + 1);
	gchar *tmp = (gchar *) g_malloc(l + 1); strcpy(tmp, ver);
	num = strtok(tmp, "."); while ((t = strtok(NULL, ".")) != NULL) strcat (num, t);
	return atoi(num);
}

void load_config (pctv_gtk_config *c) {
	gint fd, i, ov; fd = open (pctv_gtk_config_file, 0);
	if (fd < 0) {
		status ("red", "Config file not found: generating defaults..");
		create_config_defaults (c); save_config (c);
	} else {
		read (fd, pctv_gtk_version, sizeof (pctv_gtk_version)); ov = ver_to_num(pctv_gtk_version);
		strcpy(pctv_gtk_version, PCTV_GTK_VERSION);
		if (ov < 13) {
			gchar *tmp[] = { "Sorry, due to a change in config file structure your previous",
				"settings will be lost.. Be patient, this is Beta Software! :)", NULL };
			boja_popup (tmp, NULL, NULL);
			status ("red", "Config version mismatch, please check settings!");
			close (fd); create_config_defaults (c); save_config (c);
		} else {
			read (fd, c, sizeof (pctv_gtk_config));
			for (i = 0; i < MAX_CHANNELS; i++) read (fd, channel[i], sizeof (channels_map));
			close (fd); status ("blue", "Read config file..");
		}
	}
}

gboolean check_permissions (char *filename) {
	FILE *stream;
	if (access (filename, F_OK) == 0) {
		if (access (filename, W_OK) == 0) return TRUE;
		else return FALSE;
	} else {
		stream = fopen (filename, "w+"); if (stream == NULL) return FALSE;
		else {
			fclose (stream); if (access (filename, F_OK) == 0) unlink (filename); return TRUE;
		}
	}
}

void save_external_channel_file (void) {
	FILE *stream; gint i; gchar tmp[LINE_LENGTH];
	if (!check_permissions (conf->channel_file)) {
		sprintf (tmp, "Error writing external channel file %s", conf->channel_file);
		status ("red", tmp); return;
	}
	stream = fopen (conf->channel_file, "w+");
	if (stream == NULL) {
		sprintf (tmp, "Error writing external channel file %s", conf->channel_file);
		status ("red", tmp); return;
	}
	for (i = 0; i < MAX_CHANNELS; i++) {
		if (strcmp (channel[i]->name, "") == 0 && channel[i]->freq == 0) break;
		sprintf (tmp, "%s\t%d\n", channel[i]->name, channel[i]->freq); fputs (tmp, stream);
	}
	fclose (stream); sprintf (tmp, "Wrote external channel file %s", conf->channel_file);
	status ("blue", tmp);
}

void init_hardware (gboolean verbose) {
	if (fork () == 0) { execlp ("duneinit", "duneinit", NULL); }
	wait (0); if (verbose) status ("blue", "Initializing hardware..");
}

static GtkWidget *xpm_label_box(GtkWidget *window, char **xpmdata, gchar *labeltext, int space, gboolean second) {
	GtkWidget *box, *label = NULL, *image, *align;
	GdkPixmap *icon = NULL; GdkBitmap *mask;
	box = gtk_hbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(box), 1);
	align = gtk_alignment_new(0.5, 0.5, 0, 0);
	gtk_container_add(GTK_CONTAINER(align), box);
	icon = gdk_pixmap_create_from_xpm_d(window->window, &mask, NULL, xpmdata);
	image = gtk_pixmap_new(icon, mask);
	gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, space); gtk_widget_show(image);
	if(labeltext) {
		label = gtk_label_new_with_mnemonic(labeltext);
		gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
		gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, space);
		gtk_widget_show(label);
	}
	if (second) {
		icon = gdk_pixmap_create_from_xpm_d(window->window, &mask, NULL, xpmdata);
		image = gtk_pixmap_new(icon, mask); gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, space);
		gtk_widget_show(image);
	}
	gtk_widget_show(box); return align;
}

gchar *trimspace (gchar *s) {
	gint n; n = strlen (s); if (n < 1) return "\0"; gchar *t; t = (gchar *) g_malloc (n+1);
	strcpy (t, s); if (s[n-1] == ' ') { memset (t, 0, n+1); strncpy (t, s, n-1); } ; return t;
}

char *get_mplayer_modes (char *string) {
	gchar *result; gint i, j = 0; gboolean ok = 0;
	result = (gchar *) g_malloc (strlen (string)); memset (result, 0, strlen (string));
	for (i = 0; i < strlen (string); i++) {
		if (string[i] == '\t') {
			if (ok) { ok = 0; result[j++] = ' '; }
			else ok = 1;
			continue;
		}
		if (ok) result[j++] = string[i];
	}
	result[j] = '\0'; return trimspace (result);
}

void update_channels (void);

void chan_select (GtkWidget *button, gpointer chan) {
	gchar tmp[LINE_LENGTH];
	strcpy (conf->selected_channel, (gchar *) chan); save_config (conf);
	update_channels (); sprintf (tmp, "Selected channel: %s", conf->selected_channel);
	status ("blue", tmp);
}

gboolean check_read_permissions (gchar *filename) {
	if (access (filename, F_OK) == -1 || access (filename, R_OK) == -1) return FALSE;
	return TRUE;
}

gboolean load_external_chanfile (gchar *filename, gboolean import) {
	FILE *fd; gint i, n; gchar line[CHAN_LINE_LENGTH], tmp[LINE_LENGTH], tmp1[LINE_LENGTH], *s; s = NULL;
	if (strcmp (filename, "") == 0) return FALSE;
	fd = fopen (filename, "r"); if (fd == NULL) return FALSE;
	if (import) {
		for (n = 0; n < MAX_CHANNELS; n++)
			if (strcmp (channel[n]->name, "") == 0 && channel[n]->freq == 0) break;
	} else n = 0;
	for (i = n; i < MAX_CHANNELS; i++) {
		s = (gchar *) fgets (line, CHAN_LINE_LENGTH, fd);
		if (s == NULL) break;
		if (strstr (line, "\t") == NULL) {
			sprintf (tmp, "Warning! Invalid channel file format while reading '%s'", filename);
			sprintf (tmp1, "Please, check it and fix the problem (each line should contain: '%%s\\t%%d')..");
			boja_popup ((gchar *[]) { tmp, tmp1, NULL }, NULL, NULL); return FALSE;
		}
		strcpy (channel[i]->name, (gchar *) strtok (line, "\t"));
		channel[i]->freq = atoi ((gchar *) strtok (NULL, "\t"));
	}
	fclose (fd);
	if (s != NULL) {
		sprintf (tmp, "Warning! Channel File '%s' is too big, the maximum", filename);
		sprintf (tmp1, "number of allowed channels is %d, importation truncated on channel '%s'..", MAX_CHANNELS, s);
		boja_popup ((gchar *[]) { tmp, tmp1, NULL }, NULL, NULL);
	}
	if (i > 0) {
		while (i < MAX_CHANNELS) init_channel (channel[i++]);
		save_config (conf); return TRUE;
	} else return FALSE;
}

void file_ok_read (GtkWidget *widget, GtkWidget *fb) {
	gchar tmp[LINE_LENGTH], tmp1[LINE_LENGTH]; gboolean import;
	strcpy (tmp, gtk_file_selection_get_filename (GTK_FILE_SELECTION (fb)));
	if (!check_read_permissions (tmp)) {
		sprintf (tmp1, "Sorry, I can not read from file '%s'!", tmp);
		boja_popup ((gchar *[]) { tmp1, "Please, choose another file.. :)", NULL }, NULL, NULL);
		status ("red", tmp1); return;
	}
	if (strstr (gtk_window_get_title (GTK_WINDOW (fb)), "import") != NULL) import = TRUE;
	else import = FALSE;
	gtk_widget_destroy (fb);
	if (load_external_chanfile (tmp, import)) {
		update_channels ();
		sprintf (tmp1, "%s external channel file '%s'", import ? "Imported" : "Loaded", tmp);
		status ("blue", tmp1);
	} else {
		sprintf (tmp1, "Error trying to %s external channel file %s..",
			import ? "import" : "load", tmp);
		status ("red", tmp1);
	}
}

void browse_external_chan_file_load (gboolean import) {
	GtkWidget *fb; gchar tmp[LINE_LENGTH];
	sprintf (tmp, "Select external channel file to %s..", import ? "import" : "load");
	fb = gtk_file_selection_new (tmp);
	g_signal_connect_swapped (G_OBJECT (fb), "destroy", G_CALLBACK (gtk_widget_destroy), fb);
	strcpy (tmp, homedir); strcat (tmp, "/"); strcat (tmp, "channels.txt");
	gtk_file_selection_set_filename (GTK_FILE_SELECTION (fb), tmp);
	g_signal_connect (G_OBJECT (GTK_FILE_SELECTION (fb)->ok_button), "clicked", G_CALLBACK (file_ok_read), (gpointer) fb);
	g_signal_connect_swapped (G_OBJECT (GTK_FILE_SELECTION (fb)->cancel_button), "clicked", G_CALLBACK (gtk_widget_destroy), G_OBJECT (fb));
	gtk_widget_show (fb);
}

void scan_channels_dialog (void);

void update_channels (void) {
	GtkWidget *button; gint i, n, x = 0, y = 0;
	for (n = 0; n < MAX_CHANNELS; n++)
		if (strcmp (channel[n]->name, "") == 0 && channel[n]->freq == 0) break;
	if (n < 1) return;
	gtk_widget_hide (channel_table); gtk_widget_destroy (channel_table);
	channel_table = gtk_table_new (n % 3 == 0 ? (int) (n / 3) : (int) (n / 3) + 1, 3, TRUE);
	if (strcmp (conf->selected_channel, "") == 0 && channel[0]->freq != 0) {
		strcpy (conf->selected_channel, channel[0]->name); save_config (conf);
	}
	for (i = 0; i < n; i++) {
		if (strcmp (channel[i]->name, conf->selected_channel) == 0) {
			button = gtk_button_new ();
			GtkWidget *box = xpm_label_box (win, pctv_chanselect_ico_xpm, channel[i]->name, 5, TRUE);
			gtk_widget_show (box); gtk_container_add (GTK_CONTAINER (button), box);
		} else button = gtk_button_new_with_label (channel[i]->name);
		g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (chan_select), channel[i]->name);
		gtk_table_attach_defaults (GTK_TABLE (channel_table), button, x, x+1, y, y+1);
		gtk_widget_show (button); x++; if (x > 2) { y++ ; x = 0; }
	}
	gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (channel_box), channel_table);
	gtk_widget_show (channel_table);
}

GtkWidget *create_channels (char *filename) {
	gchar tmp_status[LINE_LENGTH];
	channel_box = gtk_scrolled_window_new (NULL, NULL);
	gtk_widget_set_size_request (channel_box, 512, 280);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (channel_box), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_show (channel_box); channel_table = gtk_table_new (1, 3, TRUE);
	if (conf->internal_channel_file) {
		if (channel[0]->freq == 0) {
			if (conf->channel_load_ori) {
				if (!load_external_chanfile (ORI_CHANFILE, FALSE)) {
					sprintf (tmp_status, "Channel file '%s' not found: scan channels!", ORI_CHANFILE);
					status ("red", tmp_status); scan_channels_dialog (); return channel_box;
				}
			} else {
				status ("red", "We need to scan for channels!"); scan_channels_dialog (); return channel_box;
			}
		}
	} else {
		if (!load_external_chanfile (conf->channel_file, FALSE)) {
			if (conf->channel_load_ori) {
				if (!load_external_chanfile (ORI_CHANFILE, FALSE)) {
					sprintf (tmp_status, "Channel file '%s' not found: scan channels!", ORI_CHANFILE);
					status ("red", tmp_status); scan_channels_dialog (); return channel_box;
				} else {
					sprintf (tmp_status, "Could not find external channel file '%s'", conf->channel_file);
					char tmp1[LINE_LENGTH]; sprintf (tmp1, "Loading default '%s'..", ORI_CHANFILE);
					char *text[] = { tmp_status, tmp1, NULL }; boja_popup (text, NULL, NULL);
					save_external_channel_file ();
				}
			} else {
				sprintf (tmp_status, "Channel file '%s' not found: scan channels!", conf->channel_file);
				status ("red", tmp_status); scan_channels_dialog (); return channel_box;
			}
		}
	}
	update_channels (); return channel_box;
}

void select_dunerec_input (GtkComboBox *combo, gpointer d) {
	gchar tmp[LINE_LENGTH], *data = gtk_combo_box_get_active_text(combo);
	strcpy (conf->dunerec_input, data); save_config (conf);
	sprintf (tmp, "DuneRec Input Source set to: %s", conf->dunerec_input); status ("blue", tmp);
}

void select_dunerec_quality (GtkComboBox *combo, gpointer d) {
	gchar tmp[LINE_LENGTH], *data = gtk_combo_box_get_active_text(combo);
	strcpy (conf->dunerec_quality, data); save_config (conf);
	sprintf (tmp, "DuneRec Quality set to: %s", conf->dunerec_quality); status ("blue", tmp);
}

guint get_selected_entry (gchar *options, gchar *sel) {
	guint n = 0; if (strstr(options, sel) == NULL) return n;
	gchar *ttt = (gchar *) g_malloc (strlen (options) + 1); strcpy (ttt, trimspace(options));
	gchar *tmp = (gchar *) strtok (ttt, " ");
	while (tmp != NULL) {
		if (strcmp (tmp, sel) == 0) break; tmp = (gchar *) strtok (NULL, " "); n++;
	}
	return n;
}

GtkWidget *create_option_menu (gchar *options, GCallback call, gchar *selected) {
	gchar *opt, *tmp; GtkWidget *combo;
	tmp = (gchar *) g_malloc (strlen (options) + 2); strcpy (tmp, trimspace(options)); //Backup x strtok..
	opt = (gchar *) strtok (tmp, " "); combo = gtk_combo_box_new_text();
	while (opt != NULL) {
		gtk_combo_box_append_text(GTK_COMBO_BOX(combo), opt);
		opt = (gchar *) strtok (NULL, " ");
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), get_selected_entry(options, selected));
	g_signal_connect (combo, "changed", G_CALLBACK (call), NULL);
	return combo;
}

gchar *get_format_options (void) {
	gchar *formats, line[LINE_LENGTH], tmp[LINE_LENGTH]; FILE *stream;
	formats = (gchar *) malloc (LINE_LENGTH); memset (formats, 0, sizeof (formats));
	stream = popen ("dunerec 2>&1", "r"); memset (line, 0, LINE_LENGTH);
	while (fgets (line, sizeof (line), stream) != 0) {
		if (strstr (line, "-t") != NULL) break; memset (line, 0, LINE_LENGTH);
	}
	pclose (stream);
	if (line == NULL) {
		boja_popup ((gchar *[]) { "Error retreiving dunerec -t formats, using defaults..", NULL } , NULL, NULL);
		strcpy (formats, "dvd dvdlong dvd5mbit svcd vcd svcd2 vcd2");
	} else {
		printf ("The dunerec line is: %s\n", line); // DEBUG
		strcpy (tmp, strstr (line, "<") + 1);
		strcpy (line, (gchar *) strtok (tmp, ">"));
		gchar *token = (gchar *) strtok (line, "|");
		while (token != NULL) {
			strcat (formats, token); strcat (formats, " "); token = (gchar *) strtok (NULL, "|");
		}
		g_free (token);
		if (strlen (formats) < 3) {
			boja_popup ((gchar *[]) { "Error analyzing dunerec -t formats, using defaults..", NULL } , NULL, NULL);
			strcpy (formats, "dvd dvdlong dvd5mbit svcd vcd svcd2 vcd2");
		}
	}
	return trimspace(formats);
}

GtkWidget *create_dune_options () {
	GtkWidget *vb, *hb, *frame, *label, *input, *quality;
	gchar *input_options = "Antenna SVideo Composite";
	gchar *quality_options = get_format_options ();
	frame = gtk_frame_new ("DuneRec Options");
	vb = gtk_vbox_new (TRUE, 5); hb = gtk_hbox_new (TRUE, 0);
	gtk_container_add (GTK_CONTAINER (frame), vb);
	gtk_box_pack_start (GTK_BOX (vb), hb, TRUE, TRUE, 0); gtk_widget_show (vb); gtk_widget_show (hb);
	label = gtk_label_new ("Input ->");
	gtk_label_set_markup(GTK_LABEL(label), "<span foreground=\"blue\">Input -></span>");
	input = create_option_menu (input_options, G_CALLBACK (select_dunerec_input), conf->dunerec_input);
	gtk_box_pack_start (GTK_BOX (hb), label, TRUE, TRUE, 10);
	gtk_box_pack_start (GTK_BOX (hb), input, TRUE, TRUE, 10);
	gtk_widget_show (label); gtk_widget_show (input);
	label = gtk_label_new ("Quality ->");
	gtk_label_set_markup(GTK_LABEL(label), "<span foreground=\"blue\">Quality -></span>");
	quality = create_option_menu (quality_options, G_CALLBACK (select_dunerec_quality), conf->dunerec_quality);
	gtk_box_pack_start (GTK_BOX (hb), label, TRUE, TRUE, 10);
	gtk_box_pack_start (GTK_BOX (hb), quality, TRUE, TRUE, 10);
	gtk_widget_show (label); gtk_widget_show (quality);
	return frame;
}

char *get_xine_modes (char *string, char *mode) {
	gchar *result, *line, *mode_line, *tmp_string; gint i, j = 0; gboolean found = FALSE;
	result = (gchar *) g_malloc (strlen (string)); mode_line = (gchar *) g_malloc (strlen (string));
	tmp_string = (gchar *) g_malloc (strlen (string)); strcpy (tmp_string, string);
	line = (char *) strtok (tmp_string, "\n");
	for (;;) {
		line = (char *) strtok (NULL, "\n");
		if (line == NULL) break;
		if (found) { strcpy (mode_line, line); break; }
		if (strstr (line, mode) != NULL) found = TRUE;
	}
	if (!found) strcpy (mode_line, "none");
	found = FALSE;
	for (i = 0; i < strlen (mode_line); i++) {
		if (mode_line[i] == ' ') {
			if (!found) continue;
		} else {
			if (!found) found = TRUE;
		}
		if (found) result[j++] = mode_line[i];
	}
	result[j - 1] = '\0'; result[j] = '\0' ; return trimspace(result);
}

void select_xine_audio (GtkComboBox *combo, gpointer d) {
	gchar tmp[LINE_LENGTH], *data = gtk_combo_box_get_active_text(combo);
	strcpy (conf->xine_audio_out, data); save_config (conf);
	sprintf (tmp, "Xine Audio Output set to: %s", conf->xine_audio_out); status ("blue", tmp);
}

void select_xine_video (GtkComboBox *combo, gpointer d) {
	gchar tmp[LINE_LENGTH], *data = gtk_combo_box_get_active_text(combo);
	strcpy (conf->xine_video_out, data); save_config (conf);
	sprintf (tmp, "Xine Video Output set to: %s", conf->xine_video_out); status ("blue", tmp);
}

void select_xine_deinterlace (GtkComboBox *combo, gpointer d) {
	gchar tmp[LINE_LENGTH], *data = gtk_combo_box_get_active_text(combo);
	strcpy (conf->xine_deinterlace, data); save_config (conf);
	sprintf (tmp, "Xine Deinterlace Method set to: %s", conf->xine_deinterlace); status ("blue", tmp);
}

void select_xine_postproc (GtkComboBox *combo, gpointer d) {
	gchar tmp[LINE_LENGTH], *data = gtk_combo_box_get_active_text(combo);
	strcpy (conf->xine_postprocess, data); save_config (conf);
	sprintf (tmp, "Xine PostProcess Method set to: %s", conf->xine_postprocess); status ("blue", tmp);
}

GtkWidget *create_xine_options () {
	GtkWidget *vb, *hb, *frame, *label, *audio, *video, *deint, *postproc;
	gchar *xine_audio_options, *xine_video_options;
	gchar *xine_deinterlace_options = "none bob weave greedy onefield onefield_xv linearblend";
	gchar *xine_postprocess_options = "none tvtime unsharp eq2 boxblur denoise3d eq invert expand";
	gchar line[ARG_LENGTH], tmp_line[ARG_LENGTH]; gint p[2];
	if (pipe (p) < 0) { status ("red", "Error creating pipe.."); return NULL; }
	if (fork () == 0) {
		close (p[0]); dup2 (p[1], 1); execlp ("xine", "xine", "--help", NULL);
	}
	close (p[1]); memset (line, 0, ARG_LENGTH); memset (tmp_line, 0, ARG_LENGTH); // la wait x lo Xine va dopo..
	read (p[0], tmp_line, ARG_LENGTH);
	while (strlen (tmp_line) != 0) {
		strcat (line, tmp_line); memset (tmp_line, 0, ARG_LENGTH); read (p[0], tmp_line, ARG_LENGTH);
	}
	close (p[0]); wait (0);
	xine_audio_options = get_xine_modes (line, "-A");
	xine_video_options = get_xine_modes (line, "-V");
	//printf ("Audio: ---%s---\nVideo: ---%s---\n", xine_audio_options, xine_video_options);
	frame = gtk_frame_new ("Xine Options");
	vb = gtk_vbox_new (TRUE, 5); hb = gtk_hbox_new (TRUE, 0);
	gtk_container_add (GTK_CONTAINER (frame), vb);
	gtk_box_pack_start (GTK_BOX (vb), hb, TRUE, TRUE, 0); gtk_widget_show (vb); gtk_widget_show (hb);
	label = gtk_label_new ("Audio ->");
	gtk_label_set_markup(GTK_LABEL(label), "<span foreground=\"blue\">Audio -></span>");
	audio = create_option_menu (xine_audio_options, G_CALLBACK (select_xine_audio), conf->xine_audio_out);
	gtk_box_pack_start (GTK_BOX (hb), label, TRUE, TRUE, 10);
	gtk_box_pack_start (GTK_BOX (hb), audio, TRUE, TRUE, 10);
	gtk_widget_show (label); gtk_widget_show (audio);
	label = gtk_label_new ("Video ->");
	gtk_label_set_markup(GTK_LABEL(label), "<span foreground=\"blue\">Video -></span>");
	video = create_option_menu (xine_video_options, G_CALLBACK (select_xine_video), conf->xine_video_out);
	gtk_box_pack_start (GTK_BOX (hb), label, TRUE, TRUE, 10);
	gtk_box_pack_start (GTK_BOX (hb), video, TRUE, TRUE, 10);
	gtk_widget_show (label); gtk_widget_show (video);
	hb = gtk_hbox_new (TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vb), hb, TRUE, TRUE, 0); gtk_widget_show (hb);
	label = gtk_label_new ("Deinterlace ->");
	gtk_label_set_markup(GTK_LABEL(label), "<span foreground=\"blue\">Deinterlace -></span>");
	deint = create_option_menu (xine_deinterlace_options, G_CALLBACK (select_xine_deinterlace), conf->xine_deinterlace);
	gtk_box_pack_start (GTK_BOX (hb), label, TRUE, TRUE, 10);
	gtk_box_pack_start (GTK_BOX (hb), deint, TRUE, TRUE, 10);
	gtk_widget_show (label); gtk_widget_show (deint);
	label = gtk_label_new ("Post Process ->");
	gtk_label_set_markup(GTK_LABEL(label), "<span foreground=\"blue\">Post Process -></span>");
	postproc = create_option_menu (xine_postprocess_options, G_CALLBACK (select_xine_postproc), conf->xine_postprocess);
	gtk_box_pack_start (GTK_BOX (hb), label, TRUE, TRUE, 10);
	gtk_box_pack_start (GTK_BOX (hb), postproc, TRUE, TRUE, 10);
	gtk_widget_show (label); gtk_widget_show (postproc);
	return frame;
}

void select_mplayer_audio (GtkComboBox *combo, gpointer d) {
	gchar tmp[LINE_LENGTH], *data = gtk_combo_box_get_active_text(combo);
	strcpy (conf->mplayer_audio_out, data); save_config (conf);
	sprintf (tmp, "MPlayer Audio Output set to: %s", conf->mplayer_audio_out); status ("blue", tmp);
}

void select_mplayer_video (GtkComboBox *combo, gpointer d) {
	gchar tmp[LINE_LENGTH], *data = gtk_combo_box_get_active_text(combo);
	strcpy (conf->mplayer_video_out, data); save_config (conf);
	sprintf (tmp, "MPlayer Video Output set to: %s", conf->mplayer_video_out); status ("blue", tmp);
}

void select_mplayer_deinterlace (GtkComboBox *combo, gpointer d) {
	gchar tmp[LINE_LENGTH], *data = gtk_combo_box_get_active_text(combo);
	strcpy (conf->mplayer_deinterlace, data); save_config (conf);
	sprintf (tmp, "MPlayer Deinterlace Method set to: %s", conf->mplayer_deinterlace); status ("blue", tmp);
}

void select_mplayer_postprocess (GtkWidget *widget, gpointer data) {
	gchar tmp[LINE_LENGTH];
	conf->mplayer_postprocess = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)); save_config (conf);
	sprintf (tmp, "MPlayer PostProcess %s", conf->mplayer_postprocess ? "enabled" : "disabled"); status ("blue", tmp);
}

void select_mplayer_framedrop (GtkWidget *widget, gpointer data) {
	gchar tmp[LINE_LENGTH];
	conf->mplayer_framedrop = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)); save_config (conf);
	sprintf (tmp, "MPlayer Frame Drop %s", conf->mplayer_framedrop ? "enabled" : "disabled"); status ("blue", tmp);
}

GtkWidget *create_mplayer_options () {
	GtkWidget *vb, *hb, *frame, *label, *audio, *video, *deint, *postproc, *framedrop;
	gchar *mplayer_audio_options, *mplayer_video_options, tmp_line[LINE_LENGTH];
	gchar *mplayer_deinterlace_options = "none scale lib-avc median linear cubic linearblend ffmpeg lowpass5";
	gchar line[ARG_LENGTH]; FILE *stream;
	mplayer_audio_options = (gchar *) g_malloc (LINE_LENGTH);
	mplayer_video_options = (gchar *) g_malloc (LINE_LENGTH);
	stream = popen ("mplayer -ao help", "r");
	memset (line, 0, sizeof (line)); memset (tmp_line, 0, sizeof (tmp_line));
	while (fgets (tmp_line, sizeof (tmp_line), stream) != 0) strcat (line, tmp_line);
	pclose (stream);
	strcpy (mplayer_audio_options, get_mplayer_modes (line));
	stream = popen ("mplayer -vo help", "r");
	memset (line, 0, sizeof (line)); memset (tmp_line, 0, sizeof (tmp_line));
	while (fgets (tmp_line, sizeof (tmp_line), stream) != 0) strcat (line, tmp_line);
	pclose (stream);
	strcpy (mplayer_video_options, get_mplayer_modes (line));
	//printf ("Video: ---%s---\nAudio: ---%s---\n", mplayer_video_options, mplayer_audio_options);
	frame = gtk_frame_new ("MPlayer Options");
	vb = gtk_vbox_new (TRUE, 5); hb = gtk_hbox_new (TRUE, 0);
	gtk_container_add (GTK_CONTAINER (frame), vb);
	gtk_box_pack_start (GTK_BOX (vb), hb, TRUE, TRUE, 0); gtk_widget_show (vb); gtk_widget_show (hb);
	label = gtk_label_new ("Audio ->");
	gtk_label_set_markup(GTK_LABEL(label), "<span foreground=\"blue\">Audio -></span>");
	audio = create_option_menu (mplayer_audio_options, G_CALLBACK (select_mplayer_audio), conf->mplayer_audio_out);
	gtk_box_pack_start (GTK_BOX (hb), label, FALSE, FALSE, 10);
	gtk_box_pack_start (GTK_BOX (hb), audio, TRUE, TRUE, 10);
	gtk_widget_show (label); gtk_widget_show (audio);
	label = gtk_label_new ("Video ->");
	gtk_label_set_markup(GTK_LABEL(label), "<span foreground=\"blue\">Video -></span>");
	video = create_option_menu (mplayer_video_options, G_CALLBACK (select_mplayer_video), conf->mplayer_video_out);
	gtk_box_pack_start (GTK_BOX (hb), label, FALSE, FALSE, 10);
	gtk_box_pack_start (GTK_BOX (hb), video, TRUE, TRUE, 10);
	gtk_widget_show (label); gtk_widget_show (video);
	hb = gtk_hbox_new (TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vb), hb, TRUE, TRUE, 0); gtk_widget_show (hb);
	label = gtk_label_new ("Deinterlace ->");
	gtk_label_set_markup(GTK_LABEL(label), "<span foreground=\"blue\">Deinterlace -></span>");
	deint = create_option_menu (mplayer_deinterlace_options, G_CALLBACK (select_mplayer_deinterlace), conf->mplayer_deinterlace);
	gtk_box_pack_start (GTK_BOX (hb), label, TRUE, TRUE, 10);
	gtk_box_pack_start (GTK_BOX (hb), deint, TRUE, TRUE, 10);
	gtk_widget_show (label); gtk_widget_show (deint);
	postproc = gtk_check_button_new_with_label ("Post Process");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (postproc), conf->mplayer_postprocess);
	g_signal_connect (G_OBJECT (postproc), "toggled", G_CALLBACK (select_mplayer_postprocess), NULL);
	gtk_box_pack_start (GTK_BOX (hb), postproc, TRUE, TRUE, 10); gtk_widget_show (postproc);
	framedrop = gtk_check_button_new_with_label ("Frame Drop");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (framedrop), conf->mplayer_framedrop);
	g_signal_connect (G_OBJECT (framedrop), "toggled", G_CALLBACK (select_mplayer_framedrop), NULL);
	gtk_box_pack_start (GTK_BOX (hb), framedrop, TRUE, TRUE, 10); gtk_widget_show (framedrop);
	return frame;
}

void player_not_available (gchar *player, gchar *other, GtkToggleButton *other_button) {
	gchar s1[LINE_LENGTH]; sprintf (s1, "Sorry, %s is not available.", player);
	gchar s2[LINE_LENGTH]; sprintf (s2, "Reverting choice to %s..", other);
	boja_popup ((gchar *[]) { s1, s2, NULL }, NULL, NULL);
	if (other_button != NULL) gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (other_button), TRUE);
}

void select_player (GtkToggleButton *button, GtkToggleButton *button2) {
	gchar tmp[LINE_LENGTH], player[LINE_LENGTH], other[LINE_LENGTH];
	strcpy(player, gtk_button_get_label(GTK_BUTTON(button)));
	strcpy(other, gtk_button_get_label(GTK_BUTTON(button2)));
	if (gtk_toggle_button_get_active (button)) {
		if (strcmp (player, "Xine") == 0) {
			if (xine_available) {
				strcpy (conf->selected_player, player); save_config (conf); gtk_widget_show (xineopt);
			} else player_not_available (player, other, button2);
		} else {
			if (mplayer_available) {
				strcpy (conf->selected_player, player); save_config (conf); gtk_widget_show (mpopt);
			} else player_not_available (player, other, button2);
		}
	} else { // bottone disattivo
		if (strcmp ((gchar *) player, "Xine") == 0) {
			if (mplayer_available && xineopt != NULL) gtk_widget_hide (xineopt);
		} else {
			if (xine_available && mpopt != NULL) gtk_widget_hide (mpopt);
		}
	}
	sprintf (tmp, "Selected Player: %s", conf->selected_player); status ("blue", tmp);
}

GtkWidget *create_player_sel () {
	GtkWidget *hb, *lb, *label, *mp, *xine;
	hb = gtk_hbox_new (TRUE, 0);
	label = gtk_label_new ("Favourite Player :");
	lb = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (lb), label, FALSE, FALSE, 10);
	gtk_widget_show (label);
	mp = gtk_radio_button_new_with_label (NULL, "MPlayer");
	xine = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (mp), "Xine");
	gtk_box_pack_start (GTK_BOX (hb), lb, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hb), mp, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hb), xine, TRUE, TRUE, 0);
	if (strcmp (conf->selected_player, "Xine") == 0) {
		if (xine_available) gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (xine), TRUE);
		else { // revert to mplayer
			strcpy (conf->selected_player, "MPlayer"); save_config (conf);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mp), TRUE);
		}
	} else {
		if (mplayer_available) gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mp), TRUE);
		else { // revert to xine
			strcpy (conf->selected_player, "Xine"); save_config (conf);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (xine), TRUE);
		}
	}
	g_signal_connect (G_OBJECT (mp), "toggled", G_CALLBACK (select_player), xine);
	g_signal_connect (G_OBJECT (xine), "toggled", G_CALLBACK (select_player), mp);
	gtk_widget_show (lb); gtk_widget_show (mp); gtk_widget_show (xine);
	return hb;
}

int get_chan_freq (gchar *chan) {
	gint i;
	if (strcmp (chan, "") == 0) strcpy (chan, channel[0]->name);
	for (i = 0;i < MAX_CHANNELS; i++) {
		if (strcmp (channel[i]->name, chan) == 0) break;
	}
	return channel[i]->freq;
}

char *create_dunerec_cmdline (char *filename) {
	gchar *cmd, *tmp; gint i;
	cmd = (gchar *) g_malloc (LINE_LENGTH); tmp = (gchar *) g_malloc (LINE_LENGTH);
	if (strcmp (conf->dunerec_input, "Antenna") == 0) i = 0;
	else if (strcmp (conf->dunerec_input, "SVideo") == 0) i = 1;
	else i = 2;
	sprintf (tmp, "dunerec -i %d", i); strcpy (cmd, tmp);
	if (i == 0) {
		if (conf->internal_channel_file || strcmp (conf->channel_file, ORI_CHANFILE) != 0)
			sprintf (tmp, " -a %d", get_chan_freq (conf->selected_channel));
		else sprintf (tmp, " -a %s", conf->selected_channel);
		strcat (cmd, tmp);
	}
	sprintf (tmp, " -t %s -R %s", conf->dunerec_quality, filename); strcat (cmd, tmp);
	return cmd;
}

char *create_mplayer_cmdline (char *filename) {
	gchar *cmd = (gchar *) g_malloc (LINE_LENGTH);
	sprintf (cmd, "mplayer -ao %s -vo %s -double -cache 4096", conf->mplayer_audio_out, conf->mplayer_video_out);
	if (conf->mplayer_framedrop) strcat (cmd, " -framedrop");
	if			(strcmp (conf->mplayer_deinterlace, "median") == 0) 			strcat (cmd, " -vf pp=mi");
	else if (strcmp (conf->mplayer_deinterlace, "linear") == 0) 			strcat (cmd, " -vf pp=li");
	else if (strcmp (conf->mplayer_deinterlace, "cubic") == 0) 				strcat (cmd, " -vf pp=ci");
	else if (strcmp (conf->mplayer_deinterlace, "linearblend") == 0) 	strcat (cmd, " -vf pp=lb");
	else if (strcmp (conf->mplayer_deinterlace, "ffmpeg") == 0) 			strcat (cmd, " -vf pp=fd");
	else if (strcmp (conf->mplayer_deinterlace, "lowpass5") == 0)			strcat (cmd, " -vf pp=l5");
	else if (strcmp (conf->mplayer_deinterlace, "scale") == 0) 				strcat (cmd, " -vf scale=360:288");
	else if (strcmp (conf->mplayer_deinterlace, "lib-avc") == 0) 			strcat (cmd, " -vop lavcdeint");
	if (conf->mplayer_postprocess) {
		if (strstr (cmd, "-vf pp=") != NULL) strcat (cmd, "/de");
		else if (strstr (cmd, "-vf ") != NULL) strcat (cmd, ",pp=de");
		else strcat (cmd, " -vf pp=de");
	}
	strcat (cmd, " "); strcat (cmd, filename); return cmd;
}

gboolean create_tmp_xine_conf () {
	gchar *ori_conf, *line, *tmp_line, *s, tmp_status[LINE_LENGTH]; FILE *fdr, *fdw;
	gboolean found = FALSE;
	ori_conf = (gchar *) g_malloc (strlen (conf->tmp_xineconf));
	strcpy (ori_conf, homedir); strcat (ori_conf, "/"); strcat (ori_conf, ORI_XINE_CONF);
	line = (gchar *) g_malloc (LINE_LENGTH); tmp_line = (gchar *) g_malloc (LINE_LENGTH);
	fdr = fopen (ori_conf, "r");
	if (fdr == NULL) {
		sprintf (tmp_status, "Error opening xine config file %s", ori_conf); status ("red", tmp_status); return FALSE;
	}
	fdw = fopen (conf->tmp_xineconf, "w");
	if (fdw == NULL) {
		fclose (fdr); sprintf (tmp_status, "Error opening temp xine config file %s", conf->tmp_xineconf);
		status ("red", tmp_status); return FALSE;
	}
	while (TRUE) {
		memset (line, 0, sizeof (line));
		s = (gchar *) fgets (line, LINE_LENGTH, fdr);
		if (s == NULL) break;
		//if (strstr (line, "#") != NULL) continue;
		if (strstr (line, "video.deinterlace_method") != NULL) {
			sprintf (tmp_line, "video.deinterlace_method:%s", conf->xine_deinterlace);
			fputs (tmp_line, fdw); found = TRUE; continue;
		}
		fputs (line, fdw);
	}
	if (!found) {
		sprintf (tmp_line, "video.deinterlace_method:%s", conf->xine_deinterlace); fputs (tmp_line, fdw);
	}
	fclose (fdr); fclose (fdw);
	return TRUE;
}

char *create_xine_cmdline (char *filename) {
	gchar *cmd;
	cmd = (gchar *) g_malloc (LINE_LENGTH);
	sprintf (cmd, "xine -A %s -V %s", conf->xine_audio_out, conf->xine_video_out);
	if (strcmp (conf->xine_deinterlace, "none") != 0) {
		strcat (cmd, " -D");
		// Non farglielo fare sempre!!
		if (create_tmp_xine_conf ()) { strcat (cmd, " -c "); strcat (cmd, conf->tmp_xineconf); }
	}
	if (strcmp (conf->xine_postprocess, "none") != 0) {
		strcat (cmd, " --post "); strcat (cmd, conf->xine_postprocess);
	}
	strcat (cmd, " "); strcat (cmd, filename); return cmd;
}

void update_cmdline (GtkWidget *cmd_line) {
	gchar *dunerec_cmdline, *player_cmdline, *tmp_cmdline;
	dunerec_cmdline = (gchar *) g_malloc (LINE_LENGTH);
	player_cmdline = (gchar *) g_malloc (LINE_LENGTH);
	tmp_cmdline = (gchar *) g_malloc (LINE_LENGTH);
	strcpy (dunerec_cmdline, create_dunerec_cmdline ("-"));
	if (strcmp (conf->selected_player, "Xine") == 0) strcpy (player_cmdline, create_xine_cmdline ("stdin://"));
	else strcpy (player_cmdline, create_mplayer_cmdline ("-"));
	sprintf (tmp_cmdline, "%s | %s", dunerec_cmdline, player_cmdline);
	if (cmd_line != NULL) gtk_entry_set_text (GTK_ENTRY (cmd_line), tmp_cmdline);
}

void play (GtkWidget *widget, channels_map_sel *chan) {
	gint i = 0, p[2];
	gchar *dunerec_arglist[20], *player_arglist[30], *dunerec_cmd, *player_cmd,
		*tmp, *dunerec_cmdline, *player_cmdline, *tmp_cmdline, tmp_status[LINE_LENGTH];
	dunerec_cmdline = (gchar *) g_malloc (LINE_LENGTH);
	player_cmdline = (gchar *) g_malloc (LINE_LENGTH);
	tmp_cmdline = (gchar *) g_malloc (LINE_LENGTH);
	dunerec_cmd = NULL;
	if (rec_pid == 0) {
		if (chan == NULL && (strcmp (conf->selected_channel, "") == 0) && (strcmp (conf->dunerec_input, "Antenna") == 0)) {
			strcpy (tmp_status, "You have to select a channel first! :)"); status ("red", tmp_status); return;
		}
		strcpy (tmp_cmdline, gtk_entry_get_text (GTK_ENTRY (cmdline)));
		dunerec_cmdline = (gchar *) strtok (tmp_cmdline, "|");
		if (chan != NULL)
			sprintf (dunerec_cmdline, "dunerec -i 0 -a %d -t dvd -R -", chan->freq);
		if (dunerec_cmdline == NULL) { status ("red", "Error reading dunerec command line.."); return; }
		tmp = (gchar *) strtok (dunerec_cmdline, " "); dunerec_cmd = (gchar *) g_malloc (LINE_LENGTH);
		strcpy (dunerec_cmd, tmp);
		while (tmp != NULL) {
			dunerec_arglist[i] = (gchar *) g_malloc (LINE_LENGTH); strcpy (dunerec_arglist[i++], tmp);
			tmp = (gchar *) strtok (NULL, " ");
		}
		dunerec_arglist[i] = NULL;
	}
	strcpy (tmp_cmdline, gtk_entry_get_text (GTK_ENTRY (cmdline)));
	player_cmdline = (gchar *) index (tmp_cmdline, '|');
	if (player_cmdline == NULL) { status ("red", "Error reading player command line.."); return; }
	tmp = (gchar *) strtok (player_cmdline, " "); tmp = (gchar *) strtok (NULL, " ");
	player_cmd = (gchar *) g_malloc (LINE_LENGTH); strcpy (player_cmd, tmp); i = 0;
	while (tmp != NULL) {
		player_arglist[i] = (gchar *) g_malloc (LINE_LENGTH);
		if (rec_pid != 0 && (strcmp (tmp, "-") == 0 || strcmp (tmp, "stdin://") == 0))
			strcpy (player_arglist[i++], recording_file);
		else strcpy (player_arglist[i++], tmp);
		tmp = (char *) strtok (NULL, " ");
	}
	player_arglist[i] = NULL;
	if (rec_pid == 0) {
		pipe (p);
		if (fork () == 0) {
			dup2 (p[1], 1); close (p[0]);
			execvp (dunerec_cmd, dunerec_arglist);
		}
	}
	if (fork () == 0) {
		if (rec_pid == 0) { dup2 (p[0], 0); close (p[1]); }
		execvp (player_cmd, player_arglist);
	}
	if (rec_pid == 0) {
		close (p[0]); close (p[1]); 
		if (chan == NULL) sprintf (tmp_status, "Playing channel '%s'..", conf->selected_channel);
		else sprintf (tmp_status, "Testing channel '%s'..", chan->name);
	} else sprintf (tmp_status, "Playing file %s..", recording_file);
	status ("blue", tmp_status);
}

void exec_cmd_line (GtkWidget *widget, gpointer data) {
	// To Do: parse and execute irregular command lines (rec-only, play-only, etc..).
	play (widget, data);
}

GtkWidget *create_cmdline () {
	GtkWidget *cmd;
	cmd = gtk_entry_new (); update_cmdline (cmd);
	g_signal_connect (G_OBJECT (cmd), "activate", G_CALLBACK (exec_cmd_line), NULL);
	return cmd;
}

char *rec_filename () {
	gchar *f, *tmp; time_t t; struct tm *lt;
	f = (gchar *) g_malloc (LINE_LENGTH); tmp = (gchar *) g_malloc (LINE_LENGTH);
	if (strcmp (conf->recording_dir, "~") == 0) strcpy (f, homedir);
	else strcpy (f, conf->recording_dir);
	if (f[strlen (f) - 1] != '/') strcat (f, "/");
	strcat (f, conf->selected_channel);
	t = time (NULL); lt = localtime (&t); strftime (tmp, LINE_LENGTH, "-%d-%m-%Y-%H:%M:%S", lt);
	strcat (f, tmp); strcat (f, ".mpeg");
	return f;
}

void record (GtkWidget *widget, gpointer data) {
	gchar *dunerec_arglist[20], *dunerec_cmd, *tmp, *dunerec_cmdline, *tmp_cmdline, tmp_status[LINE_LENGTH]; int i = 0;
	dunerec_cmdline = (gchar *) g_malloc (LINE_LENGTH);
	tmp_cmdline = (gchar *) g_malloc (LINE_LENGTH);
	if (rec_pid != 0) {
		sprintf (tmp_status, "Already recording file %s (pid %d): try 'Stop' first!",
			rindex (recording_file, '/') + 1, rec_pid);
		status ("red", tmp_status); return;
	} else {
		if (strcmp (conf->selected_channel, "") == 0) {
			strcpy (tmp_status, "You have to select a channel first! :)"); status ("red", tmp_status); return;
		}
	}
	strcpy (tmp_cmdline, gtk_entry_get_text (GTK_ENTRY (cmdline)));
	dunerec_cmdline = (gchar *) strtok (tmp_cmdline, "|");
	if (dunerec_cmdline == NULL) { status ("red", "Error reading dunerec command line.."); return; }
	tmp = (gchar *) strtok (dunerec_cmdline, " "); dunerec_cmd = (gchar *) g_malloc (LINE_LENGTH);
	strcpy (dunerec_cmd, tmp);
	strcpy (recording_file, rec_filename ());
	while (tmp != NULL) {
		dunerec_arglist[i] = (gchar *) g_malloc (LINE_LENGTH);
		if (strcmp (tmp, "-") == 0) strcpy (dunerec_arglist[i++], recording_file);
		else strcpy (dunerec_arglist[i++], tmp);
		tmp = (char *) strtok (NULL, " ");
	}
	dunerec_arglist[i] = NULL;
	rec_pid = fork ();
	if (rec_pid == 0) execvp (dunerec_cmd, dunerec_arglist);
	sprintf (tmp_status, "Recording channel %s on file %s (pid %d)",
		conf->selected_channel, rindex (recording_file, '/') + 1, rec_pid);
	status ("blue", tmp_status);
}

void stop (GtkWidget *widget, gpointer data) {
	gchar tmp[LINE_LENGTH];
	if (rec_pid != 0) {
		kill (rec_pid, 13); rec_pid = 0;
		sprintf (tmp, "Stopped recording file %s", rindex (recording_file, '/') + 1); status ("blue", tmp);
	} else {
		strcpy (tmp, "Nothing to stop: use this button only while recording.."); status ("red", tmp);
	}
}

GtkWidget *create_vcr () {
	GtkWidget *hb, *play_button, *rec_button, *stop_button, *box;
	hb = gtk_hbox_new (TRUE, 0);
	play_button = gtk_button_new ();
	box = xpm_label_box (win, pctv_play_ico_xpm, "Play", 10, FALSE); gtk_widget_show (box);
	gtk_container_add (GTK_CONTAINER (play_button), box);
	g_signal_connect (G_OBJECT (play_button), "clicked", G_CALLBACK (play), NULL);
	rec_button = gtk_button_new ();
	box = xpm_label_box (win, pctv_rec_ico_xpm, "Record", 10, FALSE); gtk_widget_show (box);
	gtk_container_add (GTK_CONTAINER (rec_button), box);
	g_signal_connect (G_OBJECT (rec_button), "clicked", G_CALLBACK (record), NULL);
	stop_button = gtk_button_new ();
	box = xpm_label_box (win, pctv_stop_ico_xpm, "Stop", 10, FALSE); gtk_widget_show (box);
	gtk_container_add (GTK_CONTAINER (stop_button), box);
	g_signal_connect (G_OBJECT (stop_button), "clicked", G_CALLBACK (stop), NULL);
	gtk_widget_show (play_button); gtk_widget_show (rec_button); gtk_widget_show (stop_button);
	gtk_box_pack_start (GTK_BOX (hb), play_button, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hb), rec_button, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hb), stop_button, TRUE, TRUE, 0);
	return hb;
}

void show_about () {
	GtkWidget *about_win, *label, *close_button; gchar *text;
	text = (gchar *) g_malloc (LINE_LENGTH);
	about_win = gtk_dialog_new ();
	label = gtk_label_new ("");
	sprintf (text, "<span foreground=\"red\" weight=\"bold\">Pinnacle PCTV Deluxe Console v%s by ^Boja^</span>", PCTV_GTK_VERSION);
	gtk_label_set_markup(GTK_LABEL(label), text);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (about_win)->vbox), label, TRUE, TRUE, 0);
	gtk_widget_show (label);
	label = gtk_label_new ("");
	sprintf (text, "Home Page: <span foreground=\"blue\">http://pctvgtk.sourceforge.net/</span>");
	gtk_label_set_markup(GTK_LABEL(label), text);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (about_win)->vbox), label, TRUE, TRUE, 0);
	gtk_widget_show (label);
	label = gtk_label_new ("");
	sprintf (text, "Download Page: <span foreground=\"blue\">http://sourceforge.net/project/showfiles.php?group_id=95813</span>");
	gtk_label_set_markup(GTK_LABEL(label), text);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (about_win)->vbox), label, TRUE, TRUE, 0);
	gtk_widget_show (label);
	label = gtk_label_new ("");
	sprintf (text, "Contact Author: <span foreground=\"red\">boja@libero.it - boja@avatarcorp.org</span>");
	gtk_label_set_markup(GTK_LABEL(label), text);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (about_win)->vbox), label, TRUE, TRUE, 0);
	gtk_widget_show (label);
	close_button = gtk_button_new_with_label ("Close");
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (about_win)->action_area), close_button, TRUE, TRUE, 0);
	g_signal_connect_swapped (G_OBJECT (close_button), "clicked", G_CALLBACK (gtk_widget_destroy), G_OBJECT (about_win));
	gtk_widget_show (close_button); gtk_widget_show (about_win);
}

void select_scan_source (GtkWidget *button, gpointer source) {
	gchar tmp[LINE_LENGTH];
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {
		strcpy (conf->dunerec_scan_source, (char *) source); save_config (conf);
	}
	sprintf (tmp, "Selected DuneRec Scan Source: %s", conf->dunerec_scan_source); status ("blue", tmp);
}

void select_load_channels (GtkWidget *button, gpointer data) {
	conf->channel_load_ori = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)); save_config (conf);
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
		status ("blue", "Selected to load default channel-file (if available) on empty chanlist");
	else status ("blue", "Selected to never load default channel-file");
}

void select_chan_saving (GtkWidget *button, gpointer method) {
	gchar tmp[LINE_LENGTH], tmp1[LINE_LENGTH];
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {
		if (strcmp ((char *) method, "internal") == 0) {
			conf->internal_channel_file = TRUE; strcpy (tmp1, "interally");
		} else {
			conf->internal_channel_file = FALSE; strcpy (tmp1, "to an external file");
		}
		save_config (conf); sprintf (tmp, "Selected to save channel-file %s", tmp1); status ("blue", tmp);
	}
}

void select_external_chan_saving (GtkWidget *button, gpointer hbox) {
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
		gtk_widget_show (GTK_WIDGET (hbox));
	else gtk_widget_hide (GTK_WIDGET (hbox));
}

void file_ok_sel (GtkWidget *widget, GtkWidget *fb) {
	gchar tmp[LINE_LENGTH], tmp1[LINE_LENGTH];
	strcpy (tmp, gtk_file_selection_get_filename (GTK_FILE_SELECTION (fb)));
	if (!check_permissions (tmp)) {
		sprintf (tmp1, "Sorry, I can not write on '%s'!", tmp);
		boja_popup ((gchar *[]) { tmp1, "Please, choose another file.. :)", NULL }, NULL, NULL);
		status ("red", tmp1); return;
	}
	gtk_label_set_text (GTK_LABEL (ext_chanfile_entry), tmp);
	sprintf (tmp1, "Selected '%s' as external channel file", tmp);
	status ("blue", tmp1); strcpy (conf->channel_file, tmp); save_config (conf); gtk_widget_destroy (fb);
}

void browse_external_chan_file (GtkWidget *button, gpointer data) {
	GtkWidget *fb; gchar tmp[LINE_LENGTH];
	fb = gtk_file_selection_new ("Select external channel file to save to..");
	g_signal_connect_swapped (G_OBJECT (fb), "destroy", G_CALLBACK (gtk_widget_destroy), fb);
	strcpy (tmp, homedir); strcat (tmp, "/"); strcat (tmp, "channels.txt");
	gtk_file_selection_set_filename (GTK_FILE_SELECTION (fb), tmp);
	g_signal_connect (G_OBJECT (GTK_FILE_SELECTION (fb)->ok_button), "clicked", G_CALLBACK (file_ok_sel), (gpointer) fb);
	g_signal_connect_swapped (G_OBJECT (GTK_FILE_SELECTION (fb)->cancel_button), "clicked", G_CALLBACK (gtk_widget_destroy), G_OBJECT (fb));
	gtk_widget_show (fb);
}

void abort_scan (GtkWidget *abort_button, gpointer label) {
	gtk_widget_destroy (abort_button);
	gtk_label_set_markup(GTK_LABEL(label), "Press '<span foreground=\"red\">Scan \
Channels</span>' button to begin..");
}

void bad_channel (channels_map *channel) {
	strcpy (channel->name, "Bad Channel! :("); channel->freq = 10;
}

void setup_channel (channels_map *channel, gchar *line) {
	gchar *tmp_line, *tmp;
	tmp_line = (gchar *) g_malloc (strlen (line) + 1); strcpy (tmp_line, line);
	tmp = (gchar *) strtok (tmp_line, "\t");
	if (tmp == NULL) bad_channel (channel);
	else {
		strcpy (channel->name, tmp);
		tmp = (gchar *) strtok (NULL, "\n");
		if (tmp == NULL) bad_channel (channel);
		else channel->freq = atoi (tmp);
	}
	g_free (tmp_line);
}

void get_channels (gchar *text, channels_map *chan[]) {
	gchar *line[MAX_CHANNELS]; gint i;
	i = 0; line[i] = (gchar *) strtok (text, "\n");
	while (line[i] != NULL) {
		if ((strstr (line[i], "Trying") == NULL) && (line[i][0] != '#')) i++;
		if (i >= MAX_CHANNELS) {
			gchar *tmp[] = { "Warning, too small MAX_CHANNELS variable!",
				"Please, contact the author..", NULL };
			boja_popup (tmp, NULL, NULL); break;
		}
		line[i] = (gchar *) strtok (NULL, "\n");
	}
	for (i = 0; i < MAX_CHANNELS; i++) {
		if (line[i] == NULL) break; setup_channel (chan[i], line[i]); if (chan[i]->freq == 0) break;
	}
}

void scan_channels (GtkWidget *label, channels_map *chan[]) {
	FILE *stream; gchar mode[5], list[ARG_LENGTH], tmp_line[ARG_LENGTH];
	if (rec_pid != 0) {
		gtk_label_set_markup (GTK_LABEL (label), "<span foreground=\"red\">\
Can not Scan for channels while recording!</span>");
		return;
	}
	if (strcmp (conf->dunerec_scan_source, "Antenna") == 0) strcpy (mode, "0x01");
	else strcpy (mode, "0x02");
	sprintf (tmp_line, "dunerec -i 0 -S %s", mode);
	//sprintf (tmp_line, "/home/boja/prog/c/out");
	stream = popen (tmp_line, "r");
	memset (list, 0, sizeof (list)); memset (tmp_line, 0, sizeof (tmp_line));
	while (fgets (tmp_line, sizeof (tmp_line), stream) != 0) strcat (list, tmp_line);
	pclose (stream); get_channels (list, chan);
	gtk_label_set_markup (GTK_LABEL (label), "<span foreground=\"blue\">\
Channel List retrieved successfully! ^_^</span>");
}

void modify_chan_name (GtkWidget *entry, channels_map_sel *chan) {
	char tmp[CHAN_NAME_LENGTH];
	strcpy (tmp, gtk_entry_get_text (GTK_ENTRY (entry)));
	if (strcmp (tmp, "") != 0) strcpy (chan->name, tmp);
}

void modify_chan_freq (GtkWidget *entry, channels_map_sel *chan) {
	gint tmp;
	tmp = atoi (gtk_entry_get_text (GTK_ENTRY (entry)));
	if (tmp != 0) chan->freq = tmp;
}

void toggle_chan_select (GtkWidget *button, channels_map_sel *chan) {
	chan->selected = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));
}

void add_chan_to_table (GtkWidget *button, gpointer i);

void modify_channels_table_build (channels_map *ori[], channels_map_sel *new[], gboolean first) {
	GtkWidget *scroll, *entry, *button; gint i; gchar tmp[7];
	scroll = gtk_widget_get_parent (modify_table); scroll = gtk_widget_get_parent (scroll);
	gtk_widget_hide (modify_table); gtk_widget_destroy (modify_table);
	if (first) { for (i = 0; i < MAX_CHANNELS; i++) if (ori[i]->freq == 0) break; }
	else { for (i = 0; i < MAX_CHANNELS; i++) if (new[i]->freq == 0) break; }
	modify_table = gtk_table_new (i + 2, 4, FALSE);
	gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (scroll), modify_table);
	gtk_widget_show (modify_table);
	// Table Titles
	button = gtk_button_new_with_label ("Channel Name"); gtk_widget_show (button);
	gtk_table_attach_defaults (GTK_TABLE (modify_table), button, 0, 1, 0, 1);
	button = gtk_button_new_with_label ("Frequency"); gtk_widget_show (button);
	gtk_table_attach_defaults (GTK_TABLE (modify_table), button, 1, 2, 0, 1);
	button = gtk_button_new_with_label ("Selected"); gtk_widget_show (button);
	gtk_table_attach_defaults (GTK_TABLE (modify_table), button, 2, 3, 0, 1);
	button = gtk_button_new_with_label ("Try it!"); gtk_widget_show (button);
	gtk_table_attach_defaults (GTK_TABLE (modify_table), button, 3, 4, 0, 1);
	for (i = 0; i < MAX_CHANNELS; i++) {
		if (first) { if (ori[i]->freq == 0) break; } else { if (new[i]->freq == 0) break; }
		// Name Entry
		entry = gtk_entry_new_with_max_length (CHAN_NAME_LENGTH - 1);
		gtk_entry_set_width_chars (GTK_ENTRY (entry), CHAN_NAME_LENGTH - 1);
		gtk_entry_set_text (GTK_ENTRY (entry), first ? ori[i]->name : new[i]->name);
		gtk_entry_set_editable (GTK_ENTRY (entry), TRUE); gtk_widget_show (entry);
		g_signal_connect (G_OBJECT (entry), "changed", G_CALLBACK (modify_chan_name), new[i]);
		gtk_table_attach_defaults (GTK_TABLE (modify_table), entry, 0, 1, i + 1, i + 2);
		// Frequency Entry
		entry = gtk_entry_new_with_max_length (CHAN_FREQ_LENGTH - 1);
		gtk_entry_set_width_chars (GTK_ENTRY (entry), CHAN_FREQ_LENGTH - 1);
		sprintf (tmp, "%d", first ? ori[i]->freq : new[i]->freq);
		gtk_entry_set_text (GTK_ENTRY (entry), tmp);
		gtk_entry_set_editable (GTK_ENTRY (entry), TRUE); gtk_widget_show (entry);
		g_signal_connect (G_OBJECT (entry), "changed", G_CALLBACK (modify_chan_freq), new[i]);
		gtk_table_attach_defaults (GTK_TABLE (modify_table), entry, 1, 2, i + 1, i + 2);
		// Select Flag
		button = gtk_check_button_new_with_label ("Selected");
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), new[i]->selected);
		g_signal_connect (G_OBJECT (button), "toggled", G_CALLBACK (toggle_chan_select), new[i]);
		gtk_table_attach_defaults (GTK_TABLE (modify_table), button, 2, 3, i + 1, i + 2);
		gtk_widget_show (button);
		// Test Button
		button = gtk_button_new_with_label ("Test");
		g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (play), new[i]);
		gtk_table_attach_defaults (GTK_TABLE (modify_table), button, 3, 4, i + 1, i + 2);
		gtk_widget_show (button);
	}
	button = gtk_button_new_with_label ("Add a New Channel");
	g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (add_chan_to_table), (gpointer) i);
	gtk_table_attach_defaults (GTK_TABLE (modify_table), button, 0, 4, i + 1, i + 2);
	gtk_widget_show (button);
}

void add_chan_to_table (GtkWidget *button, gpointer i) {
	strcpy (modified_channel[(gint) i]->name, "New Channel");
	modified_channel[(gint) i]->freq = 100000;
	modify_channels_table_build (channel, modified_channel, FALSE);
}

void modify_chan_select (GtkWidget *button, gpointer data) {
	gint i;
	for (i = 0; i < MAX_CHANNELS; i++) {
		if (strcmp (modified_channel[i]->name, "") == 0 && modified_channel[i]->freq == 0) break;
		if (strcmp ((gchar *) gtk_button_get_label (GTK_BUTTON (button)), "Select All") == 0)
			modified_channel[i]->selected = TRUE;
		else if (strcmp ((gchar *) gtk_button_get_label (GTK_BUTTON (button)), "Select None") == 0)
			modified_channel[i]->selected = FALSE;
		else modified_channel[i]->selected = !modified_channel[i]->selected;
	}
	modify_channels_table_build (channel, modified_channel, FALSE);
}

void modify_chan_import (GtkWidget *button, gchar *what) {
	gint i, j; gboolean found = FALSE; gchar tmp[LINE_LENGTH];
	if (strcmp (what, "keep") != 0) {
		for (i = 0; i < MAX_CHANNELS; i++)
			if (modified_channel[i]->selected) { found = TRUE; break; }
		if (!found) {
			gchar *t[] = { "Please, select at least 1 channel! :)", NULL };
			boja_popup (t, NULL, NULL); return;
		}
	}
	if (strcmp (what, "del") == 0) {
		for (i = 0; i < MAX_CHANNELS; i++) {
			if (modified_channel[i]->selected) {
				found = FALSE;
				for (j = i + 1; j < MAX_CHANNELS; j++) {
					if (!modified_channel[j]->selected) {
						strcpy (modified_channel[i]->name, modified_channel[j]->name);
						modified_channel[i]->freq = modified_channel[j]->freq;
						modified_channel[i]->selected = FALSE; modified_channel[j]->selected = TRUE;
						found = TRUE; break;
					}
				}
				if (!found) {
					memset (modified_channel[i]->name, 0, CHAN_NAME_LENGTH);
					modified_channel[i]->freq = 0; modified_channel[i]->selected = FALSE;
				}
			}
		}
		modify_channels_table_build (channel, modified_channel, FALSE);
		sprintf (tmp, "Done deleting unwanted channels");
	} else { // import sel / keep sel / keep this
		if (strstr (what, "keep") != NULL)
			for (i = 0; i < MAX_CHANNELS; i++) init_channel (channel[i]);
		if (strcmp (channel[0]->name, "") == 0 && channel[0]->freq == 0)
			for (i = 0, j = 0; j < MAX_CHANNELS; j++) {
				if (strcmp (what, "keep") == 0 || modified_channel[j]->selected) {
					strcpy (channel[i]->name, modified_channel[j]->name);
					channel[i++]->freq = modified_channel[j]->freq;
				}
			}
		else { // import sel
			for (i = 0; i < MAX_CHANNELS; i++)
				if (modified_channel[i]->selected && (modified_channel[i]->freq != 0)) {
					found = FALSE;
					for (j = 0; j < MAX_CHANNELS; j++)
						if (channel[j]->freq == modified_channel[i]->freq) { found = TRUE; break; }
					if (found) {
						strcpy (channel[j]->name, modified_channel[i]->name);
						channel[j]->freq = modified_channel[i]->freq;
					} else {
						for (j = 0; j < MAX_CHANNELS; j++)
							if (strcmp (channel[j]->name, "") == 0 && channel[j]->freq == 0) {
								strcpy (channel[j]->name, modified_channel[i]->name);
								channel[j]->freq = modified_channel[i]->freq; break;
							}
					}
				}
		}
		sprintf (tmp, "Done channel adding procedure");
	}
	save_config (conf); if (!conf->internal_channel_file) save_external_channel_file ();
	update_channels (); status ("blue", tmp);
}

void channel_swap_position (gint src, gint dest) {
	channels_map_sel *tmp;
	tmp = (channels_map_sel *) g_malloc (sizeof (channels_map_sel));
	strcpy (tmp->name, modified_channel[dest]->name);
	tmp->freq = modified_channel[dest]->freq; tmp->selected = modified_channel[dest]->selected;
	strcpy (modified_channel[dest]->name, modified_channel[src]->name);
	modified_channel[dest]->freq = modified_channel[src]->freq;
	modified_channel[dest]->selected = modified_channel[src]->selected;
	strcpy (modified_channel[src]->name, tmp->name);
	modified_channel[src]->freq = tmp->freq; modified_channel[src]->selected = tmp->selected;
	g_free (tmp);
}

void modify_chan_pos (GtkWidget *button, gchar *pos) {
	gint i, j; gboolean changed = FALSE; channels_map_sel *tmp;
	tmp = (channels_map_sel *) g_malloc (sizeof (channels_map_sel));
	if (strcmp (pos, "up") == 0)	{
		for (i = 0; i < MAX_CHANNELS; i++)
			if (modified_channel[i]->selected) {
				if ((i - 1) < 0 || modified_channel[i-1]->selected) continue;
				channel_swap_position (i, i - 1); changed = TRUE;
			}
	} else {
		for (j = 0; j < MAX_CHANNELS; j++) if (modified_channel[j]->freq == 0) break;
		for (i = j - 1; i >= 0; i--)
			if (modified_channel[i]->selected) {
				if ((i + 1) >= j || modified_channel[i+1]->selected) continue;
				channel_swap_position (i, i + 1); changed = TRUE;
			}
	}
	if (changed) modify_channels_table_build (channel, modified_channel, FALSE);
}

void modify_channels_dialog (channels_map *chan[]) {
	GtkWidget *win, *vb, *hb, *scroll, *label, *button; gint i;
	for (i = 0; i < MAX_CHANNELS; i++) {
		init_channel_sel (modified_channel[i]); strcpy (modified_channel[i]->name, chan[i]->name);
		modified_channel[i]->freq = chan[i]->freq; modified_channel[i]->selected = FALSE;
	}
	// Window Layout
	win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW (win), "Modify Channels");
	gtk_widget_set_size_request (win, 420, 480);
	g_signal_connect_swapped (G_OBJECT (win), "destroy", G_CALLBACK(gtk_widget_destroy), win);
	vb = gtk_vbox_new (FALSE, 0); gtk_container_add (GTK_CONTAINER (win), vb);
	label = gtk_label_new ("");
	gtk_label_set_markup (GTK_LABEL (label), "<span foreground=\"red\" size=\"large\"\
weight=\"bold\">You can now modify your channels</span>");
	gtk_box_pack_start (GTK_BOX (vb), label, FALSE, FALSE, 5); gtk_widget_show (label);
	// Scrolled window with channel table
	scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start (GTK_BOX (vb), scroll, TRUE, TRUE, 0); gtk_widget_show (scroll);
	modify_table = gtk_table_new (1, 4, FALSE); gtk_widget_show (modify_table);
	gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (scroll), modify_table);
	modify_channels_table_build (chan, modified_channel, TRUE);
	hb = gtk_hseparator_new (); gtk_widget_show (hb);
	gtk_box_pack_start (GTK_BOX (vb), hb, FALSE, FALSE, 0);
	// Selection Buttons
	hb = gtk_hbox_new (TRUE, 0); gtk_box_pack_start (GTK_BOX (vb), hb, FALSE, FALSE, 0);
	button = gtk_button_new_with_label ("Select All");
	g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (modify_chan_select), NULL);
	gtk_box_pack_start (GTK_BOX (hb), button, TRUE, TRUE, 0); gtk_widget_show (button);
	button = gtk_button_new_with_label ("Select None");
	g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (modify_chan_select), NULL);
	gtk_box_pack_start (GTK_BOX (hb), button, TRUE, TRUE, 0); gtk_widget_show (button);
	button = gtk_button_new_with_label ("Invert Selection");
	g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (modify_chan_select), NULL);
	gtk_box_pack_start (GTK_BOX (hb), button, TRUE, TRUE, 0); gtk_widget_show (button);
	gtk_widget_show (hb);
	// Position Buttons
	hb = gtk_hbox_new (TRUE, 0); gtk_box_pack_start (GTK_BOX (vb), hb, FALSE, FALSE, 0);
	button = gtk_button_new_with_label ("Move Up Selected");
	g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (modify_chan_pos), "up");
	gtk_box_pack_start (GTK_BOX (hb), button, TRUE, TRUE, 0); gtk_widget_show (button);
	button = gtk_button_new_with_label ("Move Down Selected");
	g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (modify_chan_pos), "down");
	gtk_box_pack_start (GTK_BOX (hb), button, TRUE, TRUE, 0); gtk_widget_show (button);
	button = gtk_button_new_with_label ("Keep This Table");
	g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (modify_chan_import), "keep");
	gtk_box_pack_start (GTK_BOX (hb), button, TRUE, TRUE, 0); gtk_widget_show (button);
	gtk_widget_show (hb);
	// Import Buttons
	hb = gtk_hbox_new (TRUE, 0); gtk_box_pack_start (GTK_BOX (vb), hb, FALSE, FALSE, 0);
	button = gtk_button_new_with_label ("Delete Selected");
	g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (modify_chan_import), "del");
	gtk_box_pack_start (GTK_BOX (hb), button, TRUE, TRUE, 0); gtk_widget_show (button);
	button = gtk_button_new_with_label ("Import Selected");
	g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (modify_chan_import), "import");
	gtk_box_pack_start (GTK_BOX (hb), button, TRUE, TRUE, 0); gtk_widget_show (button);
	button = gtk_button_new_with_label ("Keep Only Selected");
	g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (modify_chan_import), "keep_s");
	gtk_box_pack_start (GTK_BOX (hb), button, TRUE, TRUE, 0); gtk_widget_show (button);
	gtk_widget_show (hb); gtk_widget_show (vb); gtk_widget_show (win);
}

void scan_channels_interface (GtkWidget *scan_button, gpointer label) {
	GtkWidget *parent; // *abort_button, *scan_label
	channels_map *chan[MAX_CHANNELS]; int i;
	for (i = 0; i < MAX_CHANNELS; i++) {
		chan[i] = (channels_map *) g_malloc (sizeof (channels_map)); init_channel (chan[i]);
	}
	status ("blue", "Scanning Channels: be patient for at least 1 minute..");
	parent = gtk_widget_get_parent (scan_button);	// hb
	parent = gtk_widget_get_parent (parent);			// vb_win
	parent = gtk_widget_get_parent (parent);			// win della scan_channels_dialog
	/*
	gtk_widget_hide (scan_button);
	gtk_label_set_markup (label, "<span foreground=\"blue\">Current Channel Found -></span>");	
	scan_label = gtk_label_new ("Wait.. :)");
	gtk_box_pack_start (GTK_BOX (hb), scan_label, TRUE, TRUE, 0); gtk_widget_show (scan_label);
	abort_button = gtk_button_new_with_label ("Abort");
	g_signal_connect_swapped (G_OBJECT (abort_button), "clicked", G_CALLBACK (gtk_widget_show), scan_button);
	g_signal_connect_swapped (G_OBJECT (abort_button), "clicked", G_CALLBACK (gtk_widget_destroy), scan_label);
	g_signal_connect (G_OBJECT (abort_button), "clicked", G_CALLBACK (abort_scan), label);
	gtk_box_pack_end (GTK_BOX (hb), abort_button, TRUE, TRUE, 0); gtk_widget_show (abort_button);
	scan_channels (scan_label);
	*/
	scan_channels (label, chan);
	status ("blue", "Done Channel Scanning! ^_^"); gtk_widget_destroy (parent);
	modify_channels_dialog (chan); 
	update_channels ();
}

void accept_channel_options (GtkWidget *button, GtkWidget *win) {
	if (!conf->internal_channel_file) save_external_channel_file ();
	gtk_widget_destroy (win);
}

void scan_channels_dialog (void) {
	GtkWidget *win, *vb_win, *frame, *vb, *hb, *label, *antenna, *all, *internal, *external;
	GtkWidget	*check, *button, *scan_button;
	win = gtk_window_new (GTK_WINDOW_TOPLEVEL); gtk_window_set_title (GTK_WINDOW (win), "Channels Options");
	gtk_container_set_border_width (GTK_CONTAINER (win), 8);
	g_signal_connect_swapped (G_OBJECT (win), "delete_event", G_CALLBACK (gtk_widget_destroy), G_OBJECT (win));
	vb_win = gtk_vbox_new (FALSE, 0);
	// Frame DuneRec Channel Scan Options
	frame = gtk_frame_new ("DuneRec Channel Scan Options");
	gtk_box_pack_start (GTK_BOX (vb_win), frame, TRUE, TRUE, 0); gtk_widget_show (frame);
	// Scan Source
	hb = gtk_hbox_new (TRUE, 0); gtk_container_add (GTK_CONTAINER (frame), hb); gtk_widget_show (hb);
	label = gtk_label_new ("");
	gtk_label_set_markup(GTK_LABEL(label), "<span foreground=\"blue\">Select Source -></span>");
	gtk_box_pack_start (GTK_BOX (hb), label, TRUE, TRUE, 10); gtk_widget_show (label);
	antenna = gtk_radio_button_new_with_label (NULL, "Antenna Channels");
	all = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (antenna), "All Channels");
	if (strcmp (conf->dunerec_scan_source, "Antenna") == 0)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (antenna), TRUE);
	else gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (all), TRUE);
	g_signal_connect (G_OBJECT (antenna), "toggled", G_CALLBACK (select_scan_source), "Antenna");
	g_signal_connect (G_OBJECT (all), "toggled", G_CALLBACK (select_scan_source), "All");
	gtk_box_pack_start (GTK_BOX (hb), antenna, TRUE, TRUE, 10);
	gtk_box_pack_start (GTK_BOX (hb), all, TRUE, TRUE, 10);
	gtk_widget_show (antenna); gtk_widget_show (all);
	// Frame Interface Options
	frame = gtk_frame_new ("Interface	Options");
	gtk_box_pack_start (GTK_BOX (vb_win), frame, TRUE, TRUE, 0); gtk_widget_show (frame);
	vb = gtk_vbox_new (TRUE, 5); gtk_container_add (GTK_CONTAINER (frame), vb); gtk_widget_show (vb);
	// Try to load channels.txt first time?
	hb = gtk_hbox_new (TRUE, 20); gtk_box_pack_start (GTK_BOX (vb), hb, TRUE, TRUE, 0);
	gtk_widget_show (hb); check = gtk_check_button_new_with_label (
		"If available, try to load /usr/local/lib/dune/channels.txt when chanlist is empty");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), conf->channel_load_ori);
	g_signal_connect (G_OBJECT (check), "toggled", G_CALLBACK (select_load_channels), NULL);
	gtk_box_pack_start (GTK_BOX (hb), check, TRUE, TRUE, 10); gtk_widget_show (check);
	hb = gtk_hbox_new (TRUE, 0); gtk_box_pack_start (GTK_BOX (vb), hb, TRUE, TRUE, 0); gtk_widget_show (hb);
	// Save to internal conf or external filename?
	label = gtk_label_new ("");
	gtk_label_set_markup(GTK_LABEL(label), "<span foreground=\"blue\">Channel Saving Method -></span>");
	gtk_box_pack_start (GTK_BOX (hb), label, TRUE, TRUE, 10); gtk_widget_show (label);
	internal = gtk_radio_button_new_with_label (NULL, "Internal Configuration File");
	external = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (internal), "External Channel File");
	if (conf->internal_channel_file == TRUE)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (internal), TRUE);
	else gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (external), TRUE);
	g_signal_connect (G_OBJECT (internal), "toggled", G_CALLBACK (select_chan_saving), "internal");
	g_signal_connect (G_OBJECT (external), "toggled", G_CALLBACK (select_chan_saving), "external");
	gtk_box_pack_start (GTK_BOX (hb), internal, TRUE, TRUE, 10);
	gtk_box_pack_start (GTK_BOX (hb), external, TRUE, TRUE, 10);
	gtk_widget_show (internal); gtk_widget_show (external);
	// External Filename
	hb = gtk_hbox_new (FALSE, 10); gtk_box_pack_start (GTK_BOX (vb), hb, TRUE, TRUE, 0);
	label = gtk_label_new ("");
	gtk_label_set_markup(GTK_LABEL(label), "<span foreground=\"blue\">External Filename -></span>");
	gtk_box_pack_start (GTK_BOX (hb), label, TRUE, TRUE, 10); gtk_widget_show (label);
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (external))) gtk_widget_show (hb);
	g_signal_connect (G_OBJECT (external), "toggled", G_CALLBACK (select_external_chan_saving), (gpointer) hb);
	ext_chanfile_entry = gtk_label_new (conf->channel_file);
	gtk_box_pack_start (GTK_BOX (hb), ext_chanfile_entry, TRUE, TRUE, 0); gtk_widget_show (ext_chanfile_entry);
	button = gtk_button_new_with_label ("Change..");
	g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (browse_external_chan_file), NULL);
	gtk_box_pack_start (GTK_BOX (hb), button, TRUE, TRUE, 10); gtk_widget_show (button);
	// Separator and buttons
	button = gtk_hseparator_new ();
	gtk_box_pack_start (GTK_BOX (vb_win), button, TRUE, TRUE, 5); gtk_widget_show (button);
	hb = gtk_hbox_new (TRUE, 10); gtk_box_pack_start (GTK_BOX (vb_win), hb, TRUE, TRUE, 0); gtk_widget_show (hb);
	scan_button = gtk_button_new_with_label ("Scan Channels");
	gtk_box_pack_start (GTK_BOX (hb), scan_button, TRUE, TRUE, 0); gtk_widget_show (scan_button);
	button = gtk_button_new_with_label ("Accept");
	g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (accept_channel_options), win);
	gtk_box_pack_start (GTK_BOX (hb), button, TRUE, TRUE, 0); gtk_widget_show (button);
	// Channel Scan Operation
	frame = gtk_frame_new ("Channel Scan Process");
	gtk_box_pack_start (GTK_BOX (vb_win), frame, TRUE, TRUE, 0); gtk_widget_show (frame);
	hb = gtk_hbox_new (TRUE, 20); gtk_container_add (GTK_CONTAINER (frame), hb);
	gtk_widget_show (hb);
	label = gtk_label_new ("");
	gtk_label_set_markup(GTK_LABEL(label), "Press '<span foreground=\"red\">Scan \
Channels</span>' button to begin and be patient for about 1 minute..");
	gtk_box_pack_start (GTK_BOX (hb), label, TRUE, TRUE, 10); gtk_widget_show (label);
	g_signal_connect (G_OBJECT (scan_button), "clicked", G_CALLBACK (scan_channels_interface), (gpointer) label);
	gtk_container_add (GTK_CONTAINER (win), vb_win); gtk_widget_show (vb_win); gtk_widget_show (win);
}

void dir_ok_write (GtkWidget *button, GtkWidget *fb) {
	gchar dir[LINE_LENGTH], test [LINE_LENGTH];
	strcpy (dir, gtk_file_selection_get_filename (GTK_FILE_SELECTION (fb)));
	sprintf (test, "%s/pctv_testing.txt", dir);
	if (!check_permissions (test)) {
		sprintf (test, "Sorry, I can not write on dir '%s/'!", dir);
		boja_popup ((gchar *[]) { test, "Please, choose another directory.. :)", NULL }, NULL, NULL);
		status ("red", test);
	} else {
		gtk_widget_destroy (fb); strcpy (conf->recording_dir, dir); save_config (conf);
		sprintf (test, "Default recording directory set to: %s", conf->recording_dir);
		status ("blue", test);
	}
}

void select_movies_directory (void) {
	GtkWidget *fb; gchar tmp[LINE_LENGTH];
	fb = gtk_file_selection_new ("Select directory to save recorded files to..");
	g_signal_connect_swapped (G_OBJECT (fb), "destroy", G_CALLBACK (gtk_widget_destroy), fb);
	if (conf->recording_dir[0] == '~') {
		strcpy (tmp, homedir); strcat (tmp, &conf->recording_dir[1]);
	} else strcpy (tmp, conf->recording_dir);
	if (tmp[strlen (tmp) - 1] != '/') strcat (tmp, "/");
	gtk_file_selection_set_filename (GTK_FILE_SELECTION (fb), tmp);
	gtk_widget_hide (gtk_widget_get_parent(GTK_FILE_SELECTION (fb)->file_list));
	g_signal_connect (G_OBJECT (GTK_FILE_SELECTION (fb)->ok_button), "clicked", G_CALLBACK (dir_ok_write), (gpointer) fb);
	g_signal_connect_swapped (G_OBJECT (GTK_FILE_SELECTION (fb)->cancel_button), "clicked", G_CALLBACK (gtk_widget_destroy), G_OBJECT (fb));
	gtk_widget_show (fb);
}

void menu_select (char *what) {
	//g_printf ("Ho letto: %s\n", what);
	if (strcmp (what, "_About") == 0) show_about ();
	if (strcmp (what, "_Load Channel File") == 0) browse_external_chan_file_load (FALSE);
	if (strcmp (what, "_Import Channel File") == 0) browse_external_chan_file_load (TRUE);
		/*
		boja_popup (
		(gchar *[]) { "Sorry, not yet implemented..", "Please, be patient! :)", NULL },
		"Ok");
	*/
	if (strcmp (what, "_Channels Options") == 0) scan_channels_dialog ();
	if (strcmp (what, "_Modify Channels") == 0) modify_channels_dialog (channel);
	if (strcmp (what, "Movies _Directory") ==0) select_movies_directory ();
	if (strcmp (what, "_Quit") == 0) pctv_gtk_quit ();
}

GtkWidget *create_menu (char *title, char *items[], GCallback call, GtkWidget *menu_bar) {
	GtkWidget *menu, *item;
	menu = gtk_menu_new ();
	while (*items != NULL) {
		item = gtk_menu_item_new_with_mnemonic (*items);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		g_signal_connect_swapped (G_OBJECT (item), "activate", call, *items);
		gtk_widget_show (item); items++;
	}
	item = gtk_menu_item_new_with_mnemonic (title); gtk_widget_show (item);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), menu);
	if (menu_bar != NULL) gtk_menu_bar_append (GTK_MENU_BAR (menu_bar), item);
	return item;
}

GtkWidget *create_menues () {
	GtkWidget *menu_bar, *item;
	menu_bar = gtk_menu_bar_new ();
	// Functions Menu
	gchar *functions[] = { "_Load Channel File", "_Import Channel File", "_Modify Channels", "_Quit", NULL };
	item = create_menu ("_Functions", functions, G_CALLBACK (menu_select), menu_bar);
	// Preferences Menu
	gchar *prefs[] = { "_Channels Options", "Movies _Directory", NULL };
	item = create_menu ("_Preferences", prefs, G_CALLBACK (menu_select), menu_bar);
	// Help Menu
	gchar *helps[] = { "_About", NULL };
	item = create_menu ("_Help", helps, G_CALLBACK (menu_select), menu_bar);
	gtk_menu_item_right_justify (GTK_MENU_ITEM (item));	
	return menu_bar;
}

gint program_found (gchar *filename) {
	gint p[2]; gchar line[ARG_LENGTH];
	if (pipe (p) < 0) { printf ("Error creating pipe to check for program %s..\n", filename); return 0; }
	if (fork () == 0) {
		close (p[0]); dup2 (p[1], 1); dup2 (1, 2); execlp ("which", "which", filename, NULL); return 0;
	}
	close (p[1]); wait (0); memset (line, 0, sizeof (line));
	read (p[0], line, sizeof (line)); close (p[0]);
	if (strstr (line, "which: no") == NULL) return 1;
	else {
		printf ("File not found in your path: %s !\n", filename); return 0;
	}
}

gboolean check_programs () {
	gboolean ok = TRUE;
	if (!program_found ("duneinit") || !program_found ("dunerec")) {
		gchar *tmp[] = { "You have to install Dune Tools first!!!",
			"See http://www.paranoyaxc.de/dune/dune.html", NULL };
			boja_popup (tmp, "Quit", "quit"); gtk_main (); ok = FALSE;
	}
	if (!program_found ("mplayer")) mplayer_available = FALSE;
	if (!program_found ("xine")) xine_available = FALSE;
	if (!mplayer_available && !xine_available) {
		gchar *tmp[] = { "You have to install Mplayer and/or Xine first!!!",
			"See http://mplayer.hq.hu/ or http://xinehq.de/", NULL };
			boja_popup (tmp, "Quit", "quit"); gtk_main (); ok = FALSE;
	}
	return ok;
}

int main (int argc, char *argv[]) {
	GtkWidget *vb, *menues, *title, *sep, *channels, *playersel, *duneopt, *vcr, *statusbar;
	gchar title_text[LINE_LENGTH];
	gtk_init (&argc, &argv); if (!check_programs ()) return 0;
	pctv_gtk_config_file = (gchar *) g_malloc (LINE_LENGTH);
	homedir = (gchar *) g_malloc (LINE_LENGTH);
	recording_file = (gchar *) g_malloc (LINE_LENGTH);
	strcpy (homedir, (gchar *) getenv ("HOME")); strcpy (pctv_gtk_config_file, homedir);
	strcat (pctv_gtk_config_file, "/"); strcat (pctv_gtk_config_file, PCTV_GTK_CONFIG_FILE);
	conf = (pctv_gtk_config *) g_malloc (sizeof (pctv_gtk_config));
	init_channels (); load_config (conf); init_hardware (TRUE);
	win = gtk_window_new (GTK_WINDOW_TOPLEVEL); gtk_widget_realize(win);
	gtk_window_set_title (GTK_WINDOW (win), "- Pinnacle PCTV Deluxe Console -");
	g_signal_connect_swapped (G_OBJECT (win), "delete_event", G_CALLBACK (pctv_gtk_quit), NULL);
	vb = gtk_vbox_new (FALSE, 0);
	menues = create_menues ();
	gtk_box_pack_start (GTK_BOX (vb), menues, FALSE, FALSE, 0);
	gtk_widget_show(menues);
	title = gtk_label_new ("");
	// span font_desc=\"sans 14\" foreground=\"red\" weight=\"bold\"
	sprintf (title_text, "<span size=\"x-large\" foreground=\"red\" weight=\"bold\">\
Pinnacle PCTV Deluxe Console v%s</span>", PCTV_GTK_VERSION);
	gtk_label_set_markup(GTK_LABEL(title), title_text);
	statusbar = create_statusbar ();
	gtk_box_pack_end (GTK_BOX (vb), statusbar, FALSE, FALSE, 0);
	gtk_widget_show (statusbar);
	sep = gtk_hseparator_new ();
	gtk_box_pack_start (GTK_BOX (vb), title, FALSE, FALSE, 2);
	gtk_box_pack_start (GTK_BOX (vb), sep, FALSE, FALSE, 0);
	gtk_widget_show (title); gtk_widget_show (sep);
	channels = create_channels ("/usr/local/lib/dune/channels.txt");
	if (channels != NULL) {
		gtk_box_pack_start (GTK_BOX (vb), channels, FALSE, FALSE, 0);
		gtk_widget_show(channels);
	}
	sep = gtk_hseparator_new ();
	gtk_box_pack_start (GTK_BOX (vb), sep, FALSE, FALSE, 0);
	gtk_widget_show (sep);
	playersel = create_player_sel ();
	gtk_box_pack_start (GTK_BOX (vb), playersel, FALSE, FALSE, 0);
	gtk_widget_show (playersel);
	duneopt = create_dune_options ();
	gtk_box_pack_start (GTK_BOX (vb), duneopt, FALSE, FALSE, 0);
	gtk_widget_show (duneopt);
	if (xine_available) {
		xineopt = create_xine_options ();
		gtk_box_pack_start (GTK_BOX (vb), xineopt, FALSE, FALSE, 0);
		if (strcmp (conf->selected_player, "Xine") == 0) gtk_widget_show (xineopt);
	} else xineopt = NULL;
	if (mplayer_available) {
		mpopt = create_mplayer_options ();
		gtk_box_pack_start (GTK_BOX (vb), mpopt, FALSE, FALSE, 0);
		if (strcmp (conf->selected_player, "MPlayer") == 0) gtk_widget_show (mpopt);
	} else mpopt = NULL;
	cmdline = create_cmdline ();
	gtk_box_pack_start (GTK_BOX (vb), cmdline, FALSE, FALSE, 0);
	gtk_widget_show (cmdline);
	vcr = create_vcr ();
	gtk_box_pack_start (GTK_BOX (vb), vcr, FALSE, FALSE, 0);
	gtk_widget_show (vcr);
	gtk_container_add (GTK_CONTAINER (win), vb); gtk_widget_show(vb); gtk_widget_show (win);
	gtk_main (); return 0;
}
