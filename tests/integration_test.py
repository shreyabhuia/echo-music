"""Integration smoke tests: real C server, HTTP/JSON actions, real data structures, audio upload.
Run after building: python tests/integration_test.py
Uses temporary storage, never overwrites your music library.
"""
import json
import os
import pathlib
import shutil
import socket
import subprocess
import tempfile
import time
import unittest
import urllib.error
import urllib.parse
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
PORT = 18177
URL = f'http://127.0.0.1:{PORT}'


class EchoIntegration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='echo-test-')
        cls.folder = pathlib.Path(cls.temp.name)
        shutil.copytree(ROOT / 'media', cls.folder / 'media')
        shutil.copytree(ROOT / 'static', cls.folder / 'static')
        (cls.folder / 'data').mkdir()
        binary = ROOT / ('echo_server.exe' if os.name == 'nt' else 'echo_server')
        if not binary.exists():
            raise RuntimeError('Build C server before testing')
        cls.binary = binary
        cls.start()

    @classmethod
    def start(cls):
        cls.process = subprocess.Popen([str(cls.binary)], cwd=cls.folder,
                                       env={**os.environ, 'PORT': str(PORT)},
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(100):
            try:
                urllib.request.urlopen(URL + '/api/health', timeout=1).close()
                return
            except (urllib.error.URLError, TimeoutError):
                if cls.process.poll() is not None:
                    raise RuntimeError('C server stopped')
                time.sleep(.05)
        raise RuntimeError('C server did not become ready')

    @classmethod
    def stop(cls):
        cls.process.terminate()
        try:
            cls.process.wait(timeout=4)
        except subprocess.TimeoutExpired:
            cls.process.kill()

    @classmethod
    def tearDownClass(cls):
        cls.stop()
        cls.temp.cleanup()

    def req(self, path, payload=None, expected=200, headers=None, raw=False):
        data=None if payload is None else json.dumps(payload).encode()
        req=urllib.request.Request(URL+path, data=data, headers=headers or
                                   ({'Content-Type':'application/json'} if data is not None else {}))
        try:
            with urllib.request.urlopen(req, timeout=8) as resp:
                self.assertEqual(resp.status, expected)
                body=resp.read()
        except urllib.error.HTTPError as e:
            body=e.read()
            self.assertEqual(e.code, expected, body.decode())
        return body if raw else json.loads(body)

    def action(self, action, data=None, expected=200):
        return self.req('/api/action/'+action, data or {}, expected)

    def test_01_initial_state(self):
        s=self.req('/api/state')
        self.assertEqual(s['songs'],[])
        self.assertEqual(s['playlists'],[])
        self.assertIsNone(s['active_playlist'])
        self.assertEqual(self.req('/api/health')['backend'],'C')
        self.assertNotIn('total_plays', s['stats'])
        self.assertNotIn('top_played', s)
        # Populate the test fixture explicitly; the real app intentionally starts empty.
        fixtures=[('Midnight Drive','Nova Bloom','Neon Skies','Electronic'),('Golden Hour','Luna Vale','Sunset Sessions','Indie'),('City Lights','The Violet Club','After Dark','Pop'),('Dreamscape','Ari Sol','Floating','Ambient'),('Coastal Air','Echo Coast','Breezeways','Chill'),('Blue Horizon','Orion Keys','Horizons','Instrumental')]
        for title,artist,album,genre in fixtures:
            self.action('add_song',{'title':title,'artist':artist,'album':album,'genre':genre,'audio_url':'/media/midnight_drive.wav'})
        self.action('create_playlist',{'name':'My Playlist'})
        for song_id in range(1,7): self.action('add_to_playlist',{'playlist_id':1,'song_id':song_id})

    def test_02_static_html(self):
        for path,expected in [('/','Echo'),('/styles.css','sidebar'),('/app.js','function tracks')]:
            text=self.req(path,raw=True).decode()
            self.assertIn(expected,text)
        app=self.req('/app.js',raw=True).decode()
        self.assertIn('Interactive DSA visualizer',app)
        self.assertIn('data-form=\"playlist-add\"',app)
        self.assertIn('add_many_to_playlist',app)

    def test_03_hash_search(self):
        self.assertEqual(self.req('/api/search?q=3')['results'][0]['title'],'City Lights')
        self.assertEqual(self.req('/api/search?q=Orion')['results'][0]['id'],6)

    def test_04_binary_search(self):
        hits=self.req('/api/binary-search?q=Golden%20Hour')['results']
        self.assertEqual([x['id'] for x in hits],[2])
        self.assertEqual(self.req('/api/binary-search?q=not%20found')['results'],[])

    def test_05_merge_sort(self):
        songs=self.req('/api/sort?by=title')['results']
        self.assertEqual([s['title'] for s in songs],sorted((s['title'] for s in songs), key=str.casefold))
        artists=self.req('/api/sort?by=artist')['results']
        self.assertEqual([s['artist'].casefold() for s in artists],sorted(s['artist'].casefold() for s in artists))
        albums=self.req('/api/sort?by=album')['results']
        self.assertEqual([s['album'].casefold() for s in albums],sorted(s['album'].casefold() for s in albums))

    def test_06_add_song_and_search(self):
        r=self.action('add_song',{'title':'Arc & Ember','artist':'My Artist','genre':'Pop'})
        self.assertEqual(r['state']['songs'][-1]['title'],'Arc & Ember')
        self.assertEqual(self.req('/api/search?q=Ember')['results'][0]['id'],7)

    def test_07_playlist_create_add_and_duplicate(self):
        state=self.action('create_playlist',{'name':'Study Focus'})['state']
        self.assertEqual(state['active_playlist'],2)
        self.action('add_to_playlist',{'playlist_id':2,'song_id':2})
        self.action('add_to_playlist',{'playlist_id':2,'song_id':3})
        result=self.action('add_to_playlist',{'playlist_id':2,'song_id':3},400)
        self.assertIn('already',result['error'])
        # A playlist may contain only valid songs that exist in the music library.
        missing=self.action('add_to_playlist',{'playlist_id':2,'song_id':9999},400)
        self.assertIn('Song not found',missing['error'])
        self.assertEqual(self.req('/api/state')['playlists'][1]['songs'],[2,3])

    def test_08_bulk_add_to_playlist_is_validated_and_undoable(self):
        self.action('create_playlist', {'name':'Bulk Selection'})
        # A single request adds multiple existing library IDs.
        state=self.action('add_many_to_playlist', {'playlist_id':3,'song_ids':[1,2,4]})['state']
        self.assertEqual(state['playlists'][2]['songs'], [1,2,4])
        # Duplicate entries are rejected atomically; the saved order is unchanged.
        bad=self.action('add_many_to_playlist', {'playlist_id':3,'song_ids':[5,5]}, 400)
        self.assertIn('Duplicate', bad['error'])
        self.assertEqual(self.req('/api/state')['playlists'][2]['songs'], [1,2,4])
        already=self.action('add_many_to_playlist', {'playlist_id':3,'song_ids':[1,5]}, 400)
        self.assertIn('already in this playlist', already['error'])
        self.assertEqual(self.req('/api/state')['playlists'][2]['songs'], [1,2,4])
        # Invalid IDs cannot sneak songs into a playlist.
        bad=self.action('add_many_to_playlist', {'playlist_id':3,'song_ids':[5,9999]}, 400)
        self.assertIn('not in the Music Library', bad['error'])
        self.assertEqual(self.req('/api/state')['playlists'][2]['songs'], [1,2,4])
        self.action('undo')
        self.assertEqual(self.req('/api/state')['playlists'][2]['songs'], [])
        self.action('delete_playlist', {'playlist_id':3})

    def test_08_remove_undo_stack(self):
        self.action('remove_from_playlist',{'playlist_id':2,'song_id':2})
        r=self.action('undo')['state']
        self.assertEqual(r['playlists'][1]['songs'],[2,3])

    def test_09_shuffle_undo(self):
        self.action('add_to_playlist',{'playlist_id':2,'song_id':4})
        self.action('add_to_playlist',{'playlist_id':2,'song_id':5})
        before=self.req('/api/state')['playlists'][1]['songs']
        shuffled=self.action('shuffle',{'playlist_id':2})['state']['playlists'][1]['songs']
        self.assertEqual(sorted(before),sorted(shuffled))
        after=self.action('undo')['state']['playlists'][1]['songs']
        self.assertEqual(before,after)

    def test_10_linked_navigation(self):
        self.action('play',{'song_id':3})
        self.assertEqual(self.action('next')['state']['now_playing'],4)
        self.assertEqual(self.action('previous')['state']['now_playing'],3)

    def test_11_fifo(self):
        self.action('enqueue',{'song_id':6})
        self.action('enqueue',{'song_id':1})
        self.assertEqual(self.action('next')['state']['now_playing'],6)
        s=self.action('next')['state']
        self.assertEqual(s['now_playing'],1)
        self.assertEqual(s['queue'],[])

    def test_12_recent_unique_and_capped_at_15(self):
        # A -> B -> A must move A to the front without a duplicate.
        self.action('play', {'song_id': 1})
        self.action('play', {'song_id': 2})
        self.action('play', {'song_id': 1})
        recent = self.req('/api/state')['recent']
        self.assertEqual(recent[:2], [1, 2])
        self.assertEqual(len(recent), len(set(recent)))
        # Play all six IDs repeatedly; history remains unique and no longer than 15.
        for i in range(30): self.action('play', {'song_id': (i % 6) + 1})
        recent = self.req('/api/state')['recent']
        self.assertLessEqual(len(recent), 15)
        self.assertEqual(len(recent), len(set(recent)))
        self.assertEqual(recent[0], 6)

    def test_13_favorites_and_popular(self):
        self.action('toggle_favorite',{'song_id':2})
        self.assertIn(2,self.req('/api/state')['favorites'])
        self.action('toggle_favorite',{'song_id':2})
        self.assertNotIn(2,self.req('/api/state')['favorites'])
        titles=self.req('/api/sort?by=title')['results']
        self.assertEqual([s['title'] for s in titles], sorted(s['title'] for s in titles))

    def test_14_invalid_actions(self):
        self.assertEqual(self.action('play',{'song_id':9999},400)['error'],'Song not found')
        self.assertIn('name',self.action('create_playlist',{'name':''},400)['error'])

    def test_15_upload_audio(self):
        audio=(ROOT/'media/midnight_drive.wav').read_bytes()
        boundary='----ECHOBOUNDARY123'
        content=(f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="melody.wav"\r\nContent-Type: audio/wav\r\n\r\n'.encode()+audio+f'\r\n--{boundary}--\r\n'.encode())
        req=urllib.request.Request(URL+'/api/upload',content,headers={'Content-Type':f'multipart/form-data; boundary={boundary}'})
        with urllib.request.urlopen(req,timeout=10) as result:
            self.assertEqual(result.status,201)
            data=json.load(result)
        self.assertTrue(data['audio_url'].startswith('/media/user_'))
        self.assertEqual(self.req(data['audio_url'],raw=True)[:4],b'RIFF')
        self.action('add_song',{'title':'Uploaded Audio','artist':'Tester','audio_url':data['audio_url']})

    def test_16_media_range(self):
        req=urllib.request.Request(URL+'/media/city_lights.wav',headers={'Range':'bytes=0-99'})
        with urllib.request.urlopen(req,timeout=4) as result:
            self.assertEqual(result.status,206)
            self.assertEqual(len(result.read()),100)

    def test_17_persistence_after_restart(self):
        before=self.req('/api/state')
        self.stop()
        time.sleep(.15)
        self.start()
        after=self.req('/api/state')
        self.assertEqual(before['songs'],after['songs'])
        self.assertEqual(before['playlists'],after['playlists'])
        self.assertEqual(before['queue'],after['queue'])
        self.assertEqual(before['recent'],after['recent'])

    def test_18_delete_playlist_and_song(self):
        self.action('delete_playlist',{'playlist_id':2})
        s=self.action('delete_song',{'song_id':7})['state']
        self.assertEqual(len(s['playlists']),1)
        self.assertTrue(all(song['id']!=7 for song in s['songs']))


if __name__=='__main__':
    unittest.main(verbosity=2)
