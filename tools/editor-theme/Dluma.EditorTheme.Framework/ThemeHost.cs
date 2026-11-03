using System;
using System.Collections.Generic;
using System.Globalization;
using System.Threading;
using System.Windows.Forms;

namespace Dluma.EditorTheme.Framework
{
    public static class ThemeHost
    {
        private static readonly object Gate = new object();
        private static readonly Dictionary<Control, WinFormsThemeAdapter> Controls = new Dictionary<Control, WinFormsThemeAdapter>();
        private static ThemeSnapshot current;
        private static bool started, stopping, failed;
        private static int pending;

        public static int Initialize(string argument) { return SetTheme(argument, true); }
        public static int Apply(string argument) { return SetTheme(argument, false); }

        private static int SetTheme(string argument, bool initialize)
        {
            ThemeSnapshot next;
            if (!ThemeSnapshot.TryParse(argument, out next)) return -1;
            Control root = null;
            if (next.Root != IntPtr.Zero)
            {
                root = Control.FromHandle(next.Root);
                if (root == null || root.IsDisposed) return -1;
            }
            List<Control> roots;
            lock (Gate)
            {
                if (stopping || (!initialize && !started)) return -1;
                if (current != null && next.Generation < current.Generation) return 0;
                started = true;
                current = next;
                roots = new List<Control>();
                if (root != null) roots.Add(root);
                else foreach (Control item in Controls.Keys)
                    if (item.Parent == null || !Controls.ContainsKey(item.Parent)) roots.Add(item);
            }
            foreach (Control control in roots) Queue(control, delegate { TrackTree(control); });
            lock (Gate) return failed ? -1 : pending != 0 ? 1 : 0;
        }

        internal static void TrackTree(Control control)
        {
            ThemeSnapshot snapshot;
            WinFormsThemeAdapter adapter;
            lock (Gate)
            {
                if (stopping || control.IsDisposed || current == null) return;
                if (!Controls.TryGetValue(control, out adapter))
                {
                    adapter = new WinFormsThemeAdapter(control);
                    Controls.Add(control, adapter);
                }
                snapshot = current;
            }
            adapter.Apply(snapshot);
            List<Control> children = new List<Control>();
            foreach (Control child in control.Controls) children.Add(child);
            foreach (Control child in children) TrackTree(child);
        }

        internal static void Forget(Control control)
        {
            lock (Gate) Controls.Remove(control);
        }

        private static void Queue(Control control, Action action)
        {
            if (control.IsDisposed) { Forget(control); return; }
            Control dispatch = control;
            while (dispatch != null && !dispatch.IsHandleCreated) dispatch = dispatch.Parent;
            if (dispatch == null) { lock (Gate) failed = true; return; }
            if (!dispatch.InvokeRequired) { try { action(); } catch (Exception) { lock (Gate) failed = true; } return; }
            lock (Gate) ++pending;
            int completed = 0;
            EventHandler destroyed = null;
            destroyed = delegate
            {
                if (Interlocked.Exchange(ref completed, 1) != 0) return;
                dispatch.HandleDestroyed -= destroyed;
                lock (Gate) { --pending; failed = true; }
            };
            dispatch.HandleDestroyed += destroyed;
            try
            {
                dispatch.BeginInvoke((MethodInvoker)delegate
                {
                    if (Interlocked.Exchange(ref completed, 1) != 0) return;
                    dispatch.HandleDestroyed -= destroyed;
                    try { action(); }
                    catch (Exception) { lock (Gate) failed = true; }
                    finally { lock (Gate) --pending; }
                });
            }
            catch (InvalidOperationException) { destroyed(dispatch, EventArgs.Empty); }
        }

        public static int Shutdown(string argument)
        {
            if (argument != "1") return -1;
            List<KeyValuePair<Control, WinFormsThemeAdapter>> entries = null;
            lock (Gate)
            {
                if (!started) return 0;
                if (!stopping || pending == 0)
                {
                    stopping = true;
                    failed = false;
                    entries = new List<KeyValuePair<Control, WinFormsThemeAdapter>>(Controls);
                }
            }
            if (entries != null)
                foreach (KeyValuePair<Control, WinFormsThemeAdapter> entry in entries)
                {
                    WinFormsThemeAdapter adapter = entry.Value;
                    Queue(entry.Key, delegate { adapter.Restore(); Forget(entry.Key); });
                }
            lock (Gate)
            {
                if (pending != 0) return 1;
                if (Controls.Count != 0 || failed) return -1;
                started = stopping = failed = false;
                current = null;
                return 0;
            }
        }
    }

    internal sealed class ThemeSnapshot
    {
        public ulong Generation;
        public uint Kind, Preference;
        public IntPtr Root;
        public readonly System.Drawing.Color[] Colors = new System.Drawing.Color[17];
        public static bool TryParse(string argument, out ThemeSnapshot result)
        {
            result = null;
            if (argument == null || argument.Length > 512) return false;
            string[] parts = argument.Split('|');
            ulong generation, hwnd;
            uint kind, preference;
            if (parts.Length != 6 || parts[0] != "1" ||
                !ulong.TryParse(parts[1], NumberStyles.None, CultureInfo.InvariantCulture, out generation) ||
                !uint.TryParse(parts[2], NumberStyles.None, CultureInfo.InvariantCulture, out kind) || kind > 2 ||
                !uint.TryParse(parts[3], NumberStyles.None, CultureInfo.InvariantCulture, out preference) || preference > 2 ||
                parts[4].Length != 16 || !ulong.TryParse(parts[4], NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out hwnd)) return false;
            string[] colors = parts[5].Split(',');
            if (colors.Length != 17) return false;
            ThemeSnapshot parsed = new ThemeSnapshot { Generation = generation, Kind = kind, Preference = preference, Root = new IntPtr(unchecked((long)hwnd)) };
            for (int i = 0; i < colors.Length; ++i)
            {
                uint color;
                if (colors[i].Length != 8 || !uint.TryParse(colors[i], NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out color) || (color >> 24) != 255) return false;
                parsed.Colors[i] = System.Drawing.Color.FromArgb(unchecked((int)color));
            }
            result = parsed;
            return true;
        }
    }
}
